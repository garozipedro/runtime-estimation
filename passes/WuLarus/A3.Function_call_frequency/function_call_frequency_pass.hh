/*
  This file is distributed under the University of Illinois Open Source
  License. See LICENSE for details.
*/

#pragma once

#include "../A1.Branch_prediction/branch_prediction_pass.hh"
#include "../A2.Block_edge_frequency/block_edge_frequency_pass.hh"

struct FunctionCallFrequencyPass : public llvm::AnalysisInfoMixin<FunctionCallFrequencyPass> {
  using Result = FunctionCallFrequencyPass;
  typedef std::pair<const llvm::Function*, const llvm::Function*> Edge;

  Result &run(llvm::Module &, llvm::ModuleAnalysisManager &mam);
//    BranchPredictionPass *getBranchPrediction(llvm::Function *);
  double get_local_block_frequency(llvm::BasicBlock *);
  double get_local_edge_frequency(llvm::BasicBlock *, llvm::BasicBlock *);
  double get_global_block_frequency(llvm::BasicBlock *);
  double get_local_call_frequency(Edge edge);
  double get_global_call_frequency(Edge edge);
  double get_invocation_frequency(llvm::Function *node);

private:
  static llvm::AnalysisKey Key;
  friend struct llvm::AnalysisInfoMixin<FunctionCallFrequencyPass>;

  void propagate_call_freq(llvm::Function *f, llvm::Function *head, bool is_final);

  // Algorithm improvement. Traces possible functions called, and returns the frequency of their calls.
  std::vector<std::pair<llvm::Function *, double>> trace_indirect_call(llvm::CallInst *);

  // The result of Block and Edge Frequencies (Algorithm 2) for each function.
  BlockEdgeFrequencyPass *getBlockEdgeFrequency(llvm::Function *);
  std::map<llvm::Function *, BlockEdgeFrequencyPass *> function_block_edge_frequency_;

  std::map<llvm::Function *, std::set<llvm::Function *>> reachable_functions_;
  std::set<Edge> back_edges_;
  std::map<llvm::Function *, bool> visited_functions_;
  std::map<Edge, double> back_edge_prob_, lfreqs_, gfreqs_;
  std::map<llvm::Function *, double> cfreqs_; // Call frequency of each function.
};
