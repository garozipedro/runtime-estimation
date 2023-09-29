/*
  This file is distributed under the University of Illinois Open Source
  License. See LICENSE for details.
*/

#pragma once

struct BranchPredictionInfo {
    typedef std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *> Edge;
    explicit BranchPredictionInfo(llvm::DominatorTree *DT, llvm::LoopInfo *LI,
                                  llvm::PostDominatorTree *PDT = NULL);

    void buildInfo(llvm::Function &F);
    unsigned countBackEdges(llvm::BasicBlock *BB) const;
    bool callsExit(llvm::BasicBlock *BB) const;
    bool isBackEdge(const Edge &edge) const;
    bool isExitEdge(const Edge &edge) const;
    bool hasCall(const llvm::BasicBlock *BB) const;
    bool hasStore(const llvm::BasicBlock *BB) const;

    inline llvm::DominatorTree *getDominatorTree() const { return dominatorTree_; }
    inline llvm::PostDominatorTree *getPostDominatorTree() const { return postDominatorTree_; }
    inline llvm::LoopInfo *getLoopInfo() const { return loopInfo_; }

private:
    std::set<Edge> listBackEdges_, listExitEdges_;
    std::map<const llvm::BasicBlock *, unsigned> backEdgesCount_;
    std::set<const llvm::BasicBlock *> listCalls_, listStores_;

    llvm::DominatorTree *dominatorTree_;
    llvm::PostDominatorTree *postDominatorTree_;
    llvm::LoopInfo *loopInfo_;

    void findBackAndExitEdges(llvm::Function &F);
    void findCallsAndStores(llvm::Function &F);
};
