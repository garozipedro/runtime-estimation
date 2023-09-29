/*
  This file is distributed under the University of Illinois Open Source
  License. See LICENSE for details.
*/

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

#include <map>

#include "block_edge_frequency_pass.hh"

using namespace llvm;

const double BlockEdgeFrequencyPass::epsilon_ = 0.000001;

BlockEdgeFrequencyPass::Result &BlockEdgeFrequencyPass::run(Function &func, FunctionAnalysisManager &fam) {
    loopInfo_ = &fam.getResult<LoopAnalysis>(func);
    branchPredictionPass_ = new BranchPredictionPass(fam.getResult<BranchPredictionPass>(func));

    // Clear previously calculated data.
    notVisited_.clear();
    loopsVisited_.clear();
    backEdgeProbabilities_.clear();
    edgeFrequencies_.clear();
    blockFrequencies_.clear();

    // Find all loop headers of this function.
    BasicBlock *entry = nullptr;
    for (auto &BB : func.getBasicBlockList()) {
        if (entry == nullptr)
            entry = &BB;
        // If it is a loop head, add it to the list.
        if (loopInfo_->isLoopHeader(&BB))
            propagateLoop(loopInfo_->getLoopFor(&BB));
    }

    // Propagate frequencies assuming entry block is a loop head.
    markReachable(entry);
    propagateFreq(entry);

    // Clean up unnecessary information.
    notVisited_.clear();
    loopsVisited_.clear();
    backEdgeProbabilities_.clear();

    return *this;
}

/// getEdgeFrequency - Find the edge frequency based on the source and
/// the destination basic block.  If the edge is not found, return a
/// default value.
double BlockEdgeFrequencyPass::getEdgeFrequency(const BasicBlock *src,
                                                const BasicBlock *dst) const {
    // Create the edge.
    Edge edge = std::make_pair(src, dst);

    // Find the profile based on the edge.
    return getEdgeFrequency(edge);
}

/// getEdgeFrequency - Find the edge frequency based on the edge. If the
/// edge is not found, return a default value.
double BlockEdgeFrequencyPass::getEdgeFrequency(Edge &edge) const {
    // Search for the edge on the list.
    std::map<Edge, double>::const_iterator I = edgeFrequencies_.find(edge);
    return I != edgeFrequencies_.end() ? I->second : 0.0;
}

/// getBlockFrequency - Find the basic block frequency based on the edge.
/// If the basic block is not present, return a default value.
double BlockEdgeFrequencyPass::getBlockFrequency(const BasicBlock *BB) const {
    // Search for the block on the list.
    std::map<const BasicBlock *, double>::const_iterator I =
        blockFrequencies_.find(BB);
    return I != blockFrequencies_.end() ? I->second : 0.0;
}

/// getBackEdgeProbabilities - Get updated probability of back edge. In case
/// of not found, get the edge probability from the branch prediction.
double BlockEdgeFrequencyPass::getBackEdgeProbabilities(Edge &edge) {
    // Search for the back edge on the list. In case of not found, search on the
    // edge frequency list.
    std::map<Edge, double>::const_iterator I = backEdgeProbabilities_.find(edge);
    return I != backEdgeProbabilities_.end() ?
        I->second : branchPredictionPass_->getEdgeProbability(edge);
}

// updateBlockFrequency - Update BasicBlock frequency. Used by algorithm 3 to update the block
// frequencies after global function call frequencies has been calculated.
void BlockEdgeFrequencyPass::updateBlockFrequency(const llvm::BasicBlock *BB, double freq) {
    auto I = blockFrequencies_.find(BB);
    assert(I != blockFrequencies_.end() && "Trying to update unknown basic block!");
    I->second = freq;
}

/// MarkReachable - Mark all blocks reachable from root block as not visited.
void BlockEdgeFrequencyPass::markReachable(BasicBlock *root) {
    // Clear the list first.
    notVisited_.clear();

    // Use an artificial stack.
    SmallVector<BasicBlock *, 16> stack;
    stack.push_back(root);

    // Visit all childs marking them as visited in depth-first order.
    while (!stack.empty()) {
        BasicBlock *BB = stack.pop_back_val();
        if (! notVisited_.insert(BB).second)
            continue;

        // Put the new successors into the stack.
        Instruction *TI = BB->getTerminator();
        for (unsigned s = 0; s < TI->getNumSuccessors(); ++s)
            stack.push_back(TI->getSuccessor(s));
    }
}

/// PropagateLoop - Propagate frequencies from the inner most loop to the
/// outer most loop.
void BlockEdgeFrequencyPass::propagateLoop(const Loop *loop) {
    // Check if we already processed this loop.
    if (loopsVisited_.count(loop))
        return;
    // Mark the loop as visited.
    loopsVisited_.insert(loop);
    // Find the most inner loops and process them first.
    for (Loop::iterator LIT = loop->begin(), LIE = loop->end(); LIT != LIE; ++LIT) {
        const Loop *inner = *LIT;
        propagateLoop(inner);
    }

    // Find the header.
    BasicBlock *head = loop->getHeader();
    // Mark as not visited all blocks reachable from the loop head.
    markReachable(head);
    // Propagate frequencies from the loop head.
    propagateFreq(head);
}

/// PropagateFreq - Compute basic block and edge frequencies by propagating
/// frequencies.
void BlockEdgeFrequencyPass::propagateFreq(BasicBlock *head) {
    const BranchPredictionInfo *info = branchPredictionPass_->getInfo();

    // Use an artificial stack to avoid recursive calls to PropagateFreq.
    std::vector<BasicBlock *> stack;
    stack.push_back(head);

    do {
        // Get the current basic block.
        BasicBlock *BB = stack.back();
        stack.pop_back();

        // If BB has been visited.
        if (!notVisited_.count(BB))
            continue;

        // Define the block frequency. If it's a loop head, assume it executes only
        // once.
        blockFrequencies_[BB] = 1.0;

        // If it is not a loop head, calculate the block frequencies by summing all
        // edge frequencies reaching this block. If it contains back edges, take
        // into consideration the cyclic probability.
        if (BB != head) {
            // We can't calculate the block frequency if there is a back edge still
            // not calculated.
            bool InvalidEdge = false;
            for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
                 PI != PE; ++PI) {
                BasicBlock *pred = *PI;
                if (notVisited_.count(pred) &&
                    !info->isBackEdge(std::make_pair(pred, BB))) {
                    InvalidEdge = true;
                    break;
                }
            }

            // There is an unprocessed predecessor edge.
            if (InvalidEdge)
                continue;

            // Sum the incoming frequencies edges for this block. Updated
            // the cyclic probability for back edges predecessors.
            double bfreq = 0.0;
            double cyclic_probability = 0.0;

            // Verify if BB is a loop head.
            bool loop_head = loopInfo_->isLoopHeader(BB);

            // Calculate the block frequency and the cyclic_probability in case
            // of back edges using the sum of their predecessor's edge frequencies.
            for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB); PI != PE; ++PI) {
                BasicBlock *pred = *PI;

                Edge edge = std::make_pair(pred, BB);
                if (info->isBackEdge(edge) && loop_head)
                    cyclic_probability += getBackEdgeProbabilities(edge);
                else
                    bfreq += edgeFrequencies_[edge];
            }

            // For loops that seems not to terminate, the cyclic probability can be
            // higher than 1.0. In this case, limit the cyclic probability below 1.0.
            if (cyclic_probability > (1.0 - epsilon_))
                cyclic_probability = 1.0 - epsilon_;

            // Calculate the block frequency.
            blockFrequencies_[BB] = bfreq / (1.0 - cyclic_probability);
        }

        // Mark the block as visited.
        notVisited_.erase(BB);

        // Calculate the edges frequencies for all successor of this block.
        Instruction *TI = BB->getTerminator();
        for (unsigned s = 0; s < TI->getNumSuccessors(); ++s) {
            BasicBlock *successor = TI->getSuccessor(s);
            Edge edge = std::make_pair(BB, successor);
            double prob = branchPredictionPass_->getEdgeProbability(edge);

            // The edge frequency is the probability of this edge times the block
            // frequency.
            double efreq = prob * blockFrequencies_[BB];
            edgeFrequencies_[edge] = efreq;

            // If a successor is the loop head, update back edge probability.
            if (successor == head)
                backEdgeProbabilities_[edge] = efreq;

        }

        // Propagate frequencies for all successor that are not back edges.
        SmallVector<BasicBlock *, 64> backedges;
        for (unsigned s = 0; s < TI->getNumSuccessors(); ++s) {
            BasicBlock *successor = TI->getSuccessor(s);
            Edge edge = std::make_pair(BB, successor);
            if (!info->isBackEdge(edge))
                backedges.push_back(successor);
        }

        // This was done just to ensure that the algorithm would process the
        // left-most child before, to simulate normal PropagateFreq recursive calls.
        SmallVector<BasicBlock *, 64>::reverse_iterator RI, RE;
        for (RI = backedges.rbegin(), RE = backedges.rend(); RI != RE; ++RI)
            stack.push_back(*RI);
    } while (!stack.empty());
}

void BlockEdgeFrequencyPass::Clear()
{
    loopInfo_ = nullptr;
    branchPredictionPass_ = nullptr;
}

BranchPredictionPass *BlockEdgeFrequencyPass::getBranchPrediction()
{
    return branchPredictionPass_;
}

AnalysisKey BlockEdgeFrequencyPass::Key;

extern "C" LLVM_ATTRIBUTE_WEAK
llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "BlockEdgeFrequencyPass", LLVM_VERSION_STRING,
        [](PassBuilder &pb) {
            pb.registerAnalysisRegistrationCallback(
                [](FunctionAnalysisManager &fam) {
                    fam.registerPass([&] { return BlockEdgeFrequencyPass(); });
                });
        }};
}
