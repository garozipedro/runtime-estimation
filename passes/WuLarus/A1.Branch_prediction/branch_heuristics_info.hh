/*
  This file is distributed under the University of Illinois Open Source
  License. See LICENSE for details.
*/

#pragma once

#include "branch_prediction_info.hh"

// All possible branch heuristics.
enum BranchHeuristics {
    LOOP_BRANCH_HEURISTIC = 0,
    POINTER_HEURISTIC,
    CALL_HEURISTIC,
    OPCODE_HEURISTIC,
    LOOP_EXIT_HEURISTIC,
    RETURN_HEURISTIC,
    STORE_HEURISTIC,
    LOOP_HEADER_HEURISTIC,
    GUARD_HEURISTIC
};

struct BranchProbabilities {
    enum BranchHeuristics heuristic;
    const float probabilityTaken; // probability of taken a branch
    const float probabilityNotTaken; // probability of not taken a branch
    const char *name;
};

llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const BranchProbabilities& dt);

typedef std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *> Prediction;

struct BranchHeuristicsInfo {
    typedef std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *> Edge;

    explicit BranchHeuristicsInfo(BranchPredictionInfo *BPI);

    Prediction matchHeuristic(BranchHeuristics bh, llvm::BasicBlock *root) const;

    inline static unsigned getNumHeuristics() { return numBranchHeuristics_; }
    inline static enum BranchHeuristics getHeuristic(unsigned idx) { return probList[idx].heuristic; }
    inline static BranchProbabilities getBranchHeuristic(BranchHeuristics h) { return probList[h]; }
    inline static float getProbabilityTaken(enum BranchHeuristics bh) { return probList[bh].probabilityTaken; }
    inline static float getProbabilityNotTaken(enum BranchHeuristics bh) { return probList[bh].probabilityNotTaken; }
    inline static const char *getHeuristicName(enum BranchHeuristics bh) { return probList[bh].name; }

private:
    BranchPredictionInfo *branchPredictionInfo_;
    llvm::PostDominatorTree *postDominatorTree_;
    llvm::LoopInfo *loopInfo_;

    Prediction empty;

    static const unsigned numBranchHeuristics_ = 9;
    static const struct BranchProbabilities probList[numBranchHeuristics_];

    Prediction matchLoopBranchHeuristic(llvm::BasicBlock *root) const;
    Prediction matchPointerHeuristic(llvm::BasicBlock *root) const;
    Prediction matchCallHeuristic(llvm::BasicBlock *root) const;
    Prediction matchOpcodeHeuristic(llvm::BasicBlock *root) const;
    Prediction matchLoopExitHeuristic(llvm::BasicBlock *root) const;
    Prediction matchReturnHeuristic(llvm::BasicBlock *root) const;
    Prediction matchStoreHeuristic(llvm::BasicBlock *root) const;
    Prediction matchLoopHeaderHeuristic(llvm::BasicBlock *root) const;
    Prediction matchGuardHeuristic(llvm::BasicBlock *root) const;
};
