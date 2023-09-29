/*
  This file is distributed under the University of Illinois Open Source
  License. See LICENSE for details.
*/

#pragma once

#include <map>

#include "branch_heuristics_info.hh"

struct BranchPredictionPass : public llvm::AnalysisInfoMixin<BranchPredictionPass> {
    using Result = BranchPredictionPass;
    using Edge = std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *>;

    BranchPredictionPass() : branchPredictionInfo_(nullptr), branchHeuristicsInfo_(nullptr) {}
    ~BranchPredictionPass() { Clear(); }
    Result &run(llvm::Function &, llvm::FunctionAnalysisManager &);

    double getEdgeProbability(const llvm::BasicBlock *src, const llvm::BasicBlock *dst) const;
    double getEdgeProbability(const Edge &edge) const;
    const BranchPredictionInfo *getInfo() const;
    void Clear();

#ifdef SAVE_BP_TABLES
    // Save each probability before combination to print table.
    std::map<Edge, std::vector<BranchProbabilities>> edgeMatchedPredictions_;
#endif


private:
    static llvm::AnalysisKey Key;
    friend struct llvm::AnalysisInfoMixin<BranchPredictionPass>;

    BranchPredictionInfo *branchPredictionInfo_;
    BranchHeuristicsInfo *branchHeuristicsInfo_;

    std::map<Edge, double> edgeProbabilities_;

    void calculateBranchProbabilities(llvm::BasicBlock *BB);
    void addEdgeProbability(BranchHeuristics heuristic, const llvm::BasicBlock *root, Prediction pred);

    int clear_count_ = 0;
};
