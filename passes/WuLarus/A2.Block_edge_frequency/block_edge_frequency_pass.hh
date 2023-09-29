/*
  This file is distributed under the University of Illinois Open Source
  License. See LICENSE for details.
*/

#pragma once

#include "../A1.Branch_prediction/branch_prediction_pass.hh"

struct BlockEdgeFrequencyPass : public llvm::AnalysisInfoMixin<BlockEdgeFrequencyPass> {
    using Result = BlockEdgeFrequencyPass;
    using Edge = std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *>;

    Result &run(llvm::Function &f, llvm::FunctionAnalysisManager &man);

    double getEdgeFrequency(const llvm::BasicBlock *src, const llvm::BasicBlock *dst) const;
    double getEdgeFrequency(Edge &edge) const;
    double getBlockFrequency(const llvm::BasicBlock *BB) const;
    double getBackEdgeProbabilities(Edge &edge);

    void updateBlockFrequency(const llvm::BasicBlock *BB, double freq);
    ~BlockEdgeFrequencyPass() { Clear(); }
    void Clear();

    BranchPredictionPass *getBranchPrediction();

private:
    static llvm::AnalysisKey Key;
    friend struct llvm::AnalysisInfoMixin<BlockEdgeFrequencyPass>;

    static const double epsilon_;

    llvm::LoopInfo *loopInfo_;
    BranchPredictionPass *branchPredictionPass_;

    std::set<const llvm::BasicBlock *> notVisited_;
    std::set<const llvm::Loop *> loopsVisited_;
    std::map<Edge, double> backEdgeProbabilities_;
    std::map<Edge, double> edgeFrequencies_;
    std::map<const llvm::BasicBlock *, double> blockFrequencies_;

    void markReachable(llvm::BasicBlock *root);
    void propagateLoop(const llvm::Loop *loop);
    void propagateFreq(llvm::BasicBlock *head);
};
