/*
  This file is distributed under the University of Illinois Open Source
  License. See LICENSE for details.
*/

/*
  A <FREQUENCIA LOCAL DE CHAMADA> de <F> chamando <G>, eh a soma da frequencia dos blocos de <F>, que chamam <G>.
  A <FREQUENCIA GLOBAL DE CHAMADA> da funcao <F> chamando <G>, eh o numero de vezes que <F> chama <G> durante
  todas as invocacoes de <F>.
  Que eh o produto da <FREQUENCIA DE CHAMADA LOCAL> vezes a <FREQUENCIA GLOBAL DE INVOCACAO> de <F>.
*/

#include <llvm/Analysis/CallGraph.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "function_call_frequency_pass.hh"

using namespace llvm;
using namespace std;

cl::opt<bool> use_points2(
  "use-points-to-analysis",
  cl::init(false),
  cl::desc("Allow Algorithm 3 to count local frequencies of indirect function calls."),
  cl::value_desc("true or false")
);

/* Points-to analysis:
 * Improvement to allow Algorithm 3 use calls through pointers when counting local call frequecies.
***********************************************************************************************************************/
struct Points2_analysis {
  // The analysis is run on the constructor.
  Points2_analysis(FunctionCallFrequencyPass &pass, CallInst *call);

  // The functions that can be called by _call_ and their local call frequency.
  map<Function *, double> &get_result();

private:
  // Helper functions.
  // Get all predecessors of bb and predecessors of its predecessors up to the root.
  void get_ancestors(set<BasicBlock *> &ancestors, BasicBlock *bb);
  // Map all basic blocks that may set the function called by call.
  void map_functions();
  // Correct block frequency when other successor block overwrites the its effects.
  double correct_freq(BasicBlock *ref, BasicBlock *bb, bool original);

  FunctionCallFrequencyPass &pass_; // We need local block and edge frequency info.
  CallInst *call_; // The indirect call instruction whose pointer operand we are mapping to actual functions.
  double total_bfreq_; // Sum of all BB frequencies in function_map_.
  set<BasicBlock *> call_ancestors_;
  map<BasicBlock *, BasicBlock *> returns_ = {}; // Artificial edge between caller and callee's return.
  // [ref] -> [bb, function];
  map<BasicBlock *, map<BasicBlock *, Function *>> function_map_;
  // [ref] -> [bb, freq];
  map<BasicBlock *, map<BasicBlock *, double>> corrected_freqs_;
//  vector<pair<Function *, double>> result_;
  map<Function *, double> result_;
};

void print(BasicBlock *bb)
{
  bb->printAsOperand(outs(), false);
}

/* Algorithm 3.
************************************************************************************************************************
* Input:
  * A call graph, each node of which is a procedure and
  * each edge Fi -> Fj represents a call from function Fi to Fj.
  * Edge Fi -> Fj has local call frequency lfreq(Fi->Fj).

* Output:
  * Assignments of global function call frequency gfreq(Fi->Fj) to edge Fi -> Fj
  * and invocation frequency cfreq(F) to F.

* Algorithm steps:
   1. foreach edge do: back_edge_prob(edge) = lfreq(edge);
   2. foreach function f in reverse depth-first order do:
   if f is a loop head then
   mark all nodes reachable from f as not visited and all other as visited;
   propagate_call_freq(f, f, false);
   3. mark all nodes reachable from entry func as not visited and others as visited;
   4. propagate_call_freq(entry_func, entry_func, true);
*/
FunctionCallFrequencyPass::Result &FunctionCallFrequencyPass::run(Module &module, ModuleAnalysisManager &mam) {
  //CallGraph cg {module};
  //cg.print(errs());
  FunctionAnalysisManager &fam = mam.getResult<FunctionAnalysisManagerModuleProxy>(module).getManager();
  Function *entry_func = module.getFunction("main");

//  for (Function &foo : module)
//    foo.viewCFG();

  {// Step.0.
    // Get the Block and Edges Frequencies using BlockEdgeFrequencyPass for each function.
    for (Function &func : module) {
      if (func.empty() && !func.isMaterializable()) continue;
      function_block_edge_frequency_[&func] = new BlockEdgeFrequencyPass(fam.getResult<BlockEdgeFrequencyPass>(func));
    }
  }
  {// Step.1.
    for (Function &func : module) {
      visited_functions_[&func] = false;
      set<Function *> reachable_nodes = {}; // Graph nodes (functions) reachable by func.
      for (BasicBlock &bb : func) {
        for (Instruction &instr : bb) {
          if (auto *call = dyn_cast<CallInst>(&instr)) {// Find call instructions.
            // Can't directly determine the called function.
            if (!call->getCalledFunction() && use_points2) {
              Points2_analysis p2 (*this, call);
              auto traced_functions = p2.get_result();
              for (auto &traced : traced_functions) {
                outs() << "Traced call to function [" << traced.first->getName() << "] = "
                       << traced.second << "\n";
                Edge edge = make_pair(&func, traced.first);
                // TODO: sum all traced freqs and add them proportionally to call's BB bfreq.
                lfreqs_[edge] = lfreqs_[edge] + // Add block's frequency to edge.
                  traced.second;
                reachable_nodes.insert(traced.first);
              }
            }
            Edge edge = make_pair(&func, call->getCalledFunction());
            lfreqs_[edge] = lfreqs_[edge] + // Add block's frequency to edge.
              getBlockEdgeFrequency(&func)->getBlockFrequency(&bb);
            reachable_nodes.insert(call->getCalledFunction());
          }
        }
      }
      reachable_functions_[&func] = reachable_nodes;
    }
    back_edge_prob_ = lfreqs_;
  }
  {// Step.2.
    vector<Function *> dfs_functions = {};
    set<Function *> loop_heads = {};

    {// Build a list of functions reached from entry_func.
      vector<Function *> visited_stack = {}; // Detect recursion.
      dfs_functions.push_back(entry_func); // Start dfs from entry function.
      std::function<void(Function *)> dfs;
      dfs = [&](Function *foo) -> void {
        visited_stack.push_back(foo);
        for (Function *calledFunction : reachable_functions_[foo]) {
          auto found = find(dfs_functions.begin(), dfs_functions.end(), calledFunction);
          if (found == dfs_functions.end()) {
            dfs_functions.push_back(calledFunction);
            dfs(calledFunction);
          } else {// Check if it is a loop.
            for (auto f = visited_stack.rbegin(); f != visited_stack.rend(); ++f) {
              if (*f == calledFunction) {
                //errs() << "DETECTED LOOP: " << foo->getName() << " calls " << f[0]->getName() << "\n";
                loop_heads.insert(*f);
                back_edges_.insert(make_pair(foo, *f));
              }
            }
          }
        }
        visited_stack.pop_back();
      };
      dfs(dfs_functions.front());
    }

    // for (auto &edge : back_edges_) {// Print back edges found.
    //     errs() << "Back edge [" << edge.first->getName() << " -> " << edge.second->getName() << "]\n";
    // }

    // Foreach function f in reverse depth-first order do.
    for (auto f = dfs_functions.rbegin(); f != dfs_functions.rend(); ++f) {
      auto it = loop_heads.find(*f);
      if (it != loop_heads.end()) {
        Function *loop = *it;
        auto &reachable = reachable_functions_[loop]; // Nodes reachable from f.
        // Mark nodes reachable from f as not visited (false), and all others as visited (true).
        for (auto jt = visited_functions_.begin(); jt != visited_functions_.end(); ++jt) {
          jt->second = !(reachable.find(jt->first) != reachable.end());
        }
        // Mark f as not visited.
        visited_functions_[loop] = false;
        propagate_call_freq(loop, loop, false);
      }
    }
  }
  {// Step.3.
    auto &reachable = reachable_functions_[entry_func];
    for (auto jt = visited_functions_.begin(); jt != visited_functions_.end(); ++jt) {
      jt->second = !(reachable.find(jt->first) != reachable.end());
    }
    visited_functions_[entry_func] = false;
  }
  {// Step.4.
    propagate_call_freq(entry_func, entry_func, true);
    // TODO: update gfreq.
  }
  // Sum incoming cfreqs for functions not propagated to.
  for (Function &func : module) {
    if (cfreqs_.find(&func) == cfreqs_.end()) {
      for (auto reachables : reachable_functions_) {
        if (&func != reachables.first && (reachables.second.find(&func) != reachables.second.end())) {
          // reachables.first is a precedessor of func.
          Edge edge = make_pair(reachables.first, &func);
          cfreqs_[&func] += gfreqs_[edge];
        }
      }
    }
  }
  return *this;
}

void FunctionCallFrequencyPass::propagate_call_freq(Function *f, Function *head, bool is_final) {
  const double epsilon = 0.000001;

  if (visited_functions_[f]) return;

  {// 1. Find cfreq(f).
    vector<Function *> fpreds = {}; // Predecessors of f.
    for (auto it = reachable_functions_.begin(); it != reachable_functions_.end(); ++it)
      if (it->second.find(f) != it->second.end()) {// f is called by it->first.
        Function *fp = it->first;
        Edge fp_f = make_pair(fp, f);
        if (!visited_functions_[fp] && (back_edges_.find(fp_f) == back_edges_.end())) return;
        fpreds.push_back(it->first);
      }
    cfreqs_[f] = (f == head ? 1 : 0);
    double cyclic_probability = 0;
    for (auto fp : fpreds) {
      Edge fp_f = make_pair(fp, f);
      if (is_final && (back_edges_.find(fp_f) != back_edges_.end()))
        cyclic_probability += back_edge_prob_[fp_f];
      else if (back_edges_.find(fp_f) == back_edges_.end())
        cfreqs_[f] += gfreqs_[fp_f];
    }
    if (cyclic_probability > 1 - epsilon) cyclic_probability = 1 - epsilon;
    cfreqs_[f] = cfreqs_[f] / (1.0 - cyclic_probability);
  }
  {// 2. Calculate global call frequencies for f's out edges.
    visited_functions_[f] = true;
    for (auto fi : reachable_functions_[f]) {
      Edge f_fi = make_pair(f, fi);
      gfreqs_[f_fi] = lfreqs_[f_fi] * cfreqs_[f];
      if (fi == head && !is_final) back_edge_prob_[f_fi] = lfreqs_[f_fi] * cfreqs_[f];
    }
  }
  {// 3. Propagate to successor nodes.
    for (auto fi : reachable_functions_[f]) {
      Edge f_fi = make_pair(f, fi);
      if (back_edges_.find(f_fi) == back_edges_.end()) {
        propagate_call_freq(fi, head, is_final);
      }
    }
  }
}

double FunctionCallFrequencyPass::get_local_block_frequency(llvm::BasicBlock *bb)
{
  auto a2_analysis = getBlockEdgeFrequency(bb->getParent());
  if (a2_analysis) return a2_analysis->getBlockFrequency(bb);
  return 0;
}

double FunctionCallFrequencyPass::get_local_edge_frequency(llvm::BasicBlock *src, llvm::BasicBlock *dst)
{
  assert(src->getParent() == dst->getParent() && "<src> and <dst> must be in the same function!");
  auto a2_analysis = getBlockEdgeFrequency(src->getParent());
  if (a2_analysis) return a2_analysis->getEdgeFrequency(src, dst);
  return 0;
}

double FunctionCallFrequencyPass::get_global_block_frequency(llvm::BasicBlock *bb)
{
  auto a2_analysis = getBlockEdgeFrequency(bb->getParent());
  if (a2_analysis) return a2_analysis->getBlockFrequency(bb) * get_invocation_frequency(bb->getParent());
  return 0;
}


BlockEdgeFrequencyPass *FunctionCallFrequencyPass::getBlockEdgeFrequency(Function *func)
{
  auto found = function_block_edge_frequency_.find(func);
  if (found == function_block_edge_frequency_.end()) return nullptr;
  return found->second;
}

double FunctionCallFrequencyPass::get_local_call_frequency(Edge edge)
{
  auto found = lfreqs_.find(edge);
  if (found == lfreqs_.end()) return 0;
  return found->second;
}

double FunctionCallFrequencyPass::get_global_call_frequency(Edge edge)
{
  auto found = gfreqs_.find(edge);
  if (found == gfreqs_.end()) return 0;
  return found->second;
}

double FunctionCallFrequencyPass::get_invocation_frequency(Function *node)
{
  auto found = cfreqs_.find(node);
  if (found == cfreqs_.end()) return 0;
  return found->second;
}

AnalysisKey FunctionCallFrequencyPass::Key;

extern "C" LLVM_ATTRIBUTE_WEAK
llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "FunctionCallFrequencyPass", LLVM_VERSION_STRING,
    [](PassBuilder &pb) {
      pb.registerAnalysisRegistrationCallback(
        [](ModuleAnalysisManager &man) {
          man.registerPass([&] { return FunctionCallFrequencyPass(); });
        });
    }};
}

/* Points-to analysis:
***********************************************************************************************************************/
// Trace indirect call by finding which basic blocks contain stores to the memory location from which
// the function address is loaded.
// Algorithm:
//   Input: the indirect call instruction _call_;
//   Output: result, all the functions that can be called by _call_ and the local frequency for the call site;
// Definitions:
//  * _cbb_ is the BB of _call_;
//  * _CP_ is the set of all BB that may reach _cbb_ and _cbb_ itself;
//  * _SI_ is the set of all store instructions that may determine the function called by _call_;
//  * _CF_ is the result of correcting _SI_;
// Subroutine correct_store_frequency(BB bb) -> double:
//     Input: a BB bb from _CP_;
//     Output: _CF_, the set of corrected _SI_ frequencies according to paths to _cbb_ where the stores are not overwritten;
//       .Note that _CF_ is polluted by memoization of intermediate BBs.
//   bb is the basic block of si;
//   if bb is in _CF_, then return _CF_[bb];
//   if bb == _cbb_, then return bfreq(bb);
//   _CF_[bb] = 0;
//   for each successor s of bb:
//     if s == _cbb_, then _CF_[bb] += freq(bb, s);
//     else _CF_[bb] += correct_store_frequency(s);
//   return _CF_[bb];
// Step 1. Correct all frequencies from _SI_:
//   for each si from _SI_: correct_store_frequency(si->getParent());
// Step 2. Filter the BBs from _CF_ that were originally from _SI_, they are the result:
//   for each si from _SI_:
//     function = si->getValueOperand();
//     result[function] = _CF_[si->getParent()];
Points2_analysis::Points2_analysis(FunctionCallFrequencyPass &pass, CallInst *call) :
  pass_(pass), call_(call)
{
  outs() << "\n\n\n***[ Tracing : " << *call << "]***\n";

  // Definitions.
  BasicBlock *call_bb = call->getParent(); // _cbb_.
  double total_stores_freq = 0; // Frequency sum of all BB with relevant stores.

  get_ancestors(call_ancestors_, call_bb);
  {// Print.
    outs() << "call ancestors = {";
    for (auto pred : call_ancestors_) {
      outs() << " ";
      pred->printAsOperand(outs(), false);
    } outs() << " }\n";
  }
  map_functions();
/*
  total_bfreq_ = 0;
  for (auto &it : function_map_)
    total_bfreq_ += pass.get_local_block_frequency(it.first);
*/
/*
  {// Print.
    outs() << "Mapped lfreqs:\n";
    for (auto it : function_map_) {
      outs() << "\tbfreq(" << it.first->getParent()->getName() << "/";
      it.first->printAsOperand(outs(), false);
      outs() << ") = " << pass.get_local_block_frequency(it.first) << " => ";
      if (it.second) outs() << it.second->getName() << "\n";
      else outs() << "NULL\n";
    }
  }
*/
  for (auto &it : function_map_)
    for (auto &jt : it.second)
      correct_freq(it.first, jt.first, true);
  {// Print.
    outs() << "PRINTING CORRECTED FREQS:\n";
    for (auto &it : function_map_)
      for (auto &jt : it.second) {
        outs() << "("; print(it.first);
        outs() << "/"; print(jt.first);
        if (jt.second) outs() << "/" << jt.second->getName();
        else outs() << "/NULL";
        outs() << ") = " << corrected_freqs_[it.first][jt.first] << "\n";
      }
  }
  {// Push corrected frequencies to result_.
    for (const auto &it : function_map_) {
      BasicBlock *ref = it.first;
      for (const auto &jt : it.second) {
        if (!jt.second) continue; // Don't push null.
        if (!result_.count(jt.second)) result_[jt.second] = 0;
        result_[jt.second] += corrected_freqs_[ref][jt.first];
      }
    }
  }
}

map<Function *, double> &Points2_analysis::get_result() { return result_; }

void Points2_analysis::get_ancestors(set<BasicBlock *> &set, BasicBlock *bb)
{
  set.insert(bb);
  for (auto pred : predecessors(bb))
    if (!set.count(pred)) {
      get_ancestors(set, pred);
    }
}

void Points2_analysis::map_functions()
{
    Instruction *callee = dyn_cast<Instruction>(call_->getCalledOperand());
    deque<Instruction *> instructions = {callee};
    LoadInst *ref_load = nullptr;
    set<BasicBlock *> ref_load_ancestors;
    outs() << "MAPPING BLOCKS TO FUNCTIONS\n";
    outs() << "***************************\n";
    while (!instructions.empty()) {
      Instruction *curri = instructions.back();
      instructions.pop_back();
      {// Print.
        outs() << "-> [" << *curri << "]\n";
      }
      if (auto *alloca_instr = dyn_cast<AllocaInst>(curri)) {// Follow stores to alloca's value.
        assert(ref_load);
        map<BasicBlock *, StoreInst *> stores;
        for (User *user : alloca_instr->users()) {
          if (auto *store_instr = dyn_cast<StoreInst>(user)) {
            outs() << "STORE [";
            print(dyn_cast<Instruction>(user)->getParent());
            outs() << "/" << *user << "]\n";
            if ((store_instr->getParent() == ref_load->getParent() && !store_instr->comesBefore(ref_load)) ||
              !ref_load_ancestors.count(store_instr->getParent())) continue;
            bool insert_store_cond =
              // There is no other store instruction in the same BB.
              stores.find(store_instr->getParent()) == stores.end() ||
              // store_instr comes after the currently last visited store instruction from store_instr's BB.
              stores[store_instr->getParent()]->comesBefore(store_instr);
            if (insert_store_cond) stores[store_instr->getParent()] = store_instr;
          }
        }
        for (auto &store : stores) {// Filter final (function or NULL) and non final stores.
          if (auto *instr = dyn_cast<Instruction>(store.second->getValueOperand())) {
            instructions.push_back(instr);
          } else {//TODO: else if dyn_cast<Function>(...
            function_map_[ref_load->getParent()].emplace(
              store.first, dyn_cast<Function>(store.second->getValueOperand()));
            outs() << "Inserting [";
            print(store.first);
            outs() << "/" << *store.second << "\n";
          }
        }
      } else if (auto *load_instr = dyn_cast<LoadInst>(curri)) {// Follow load operand.
        ref_load = load_instr;
        outs() << "Ref load now is [" << *ref_load << "]\n";
        instructions.push_back(dyn_cast<Instruction>(load_instr->getPointerOperand()));
        ref_load_ancestors.clear();
        get_ancestors(ref_load_ancestors, ref_load->getParent());
      } else if (auto *call_instr = dyn_cast<CallInst>(curri)) {// Follow call's return.
        if (Function *callee = dyn_cast<Function>(call_instr->getCalledOperand())) {
          for (BasicBlock &bb : *callee) {
            call_ancestors_.insert(&bb); // All BB from this function are now ancestors to the caller.
            for (Instruction &inst : bb)
              if (auto *ret = dyn_cast<ReturnInst>(&inst)) {// Get function's return instruction.
                returns_[&bb] = call_instr->getParent();
                if (auto *instr = dyn_cast<Instruction>(ret->getReturnValue())) {
                  instructions.push_back(instr);
                } else if (auto *func = dyn_cast<Function>(ret->getReturnValue())) {
                  function_map_[ref_load->getParent()].emplace(curri->getParent(), func);
                }
              }
          }
        }
      }
    }
    outs() << "FINAL MAP\n";
    outs() << "*********\n";
    for (auto &it : function_map_) {
      outs() << "[";
      print(it.first);
      outs() << "] \n{";
      for (auto &jt : it.second) {
        outs() << "\t[";
        print(jt.first);
        outs() << "] = (";
        if (jt.second) outs() << jt.second->getName();
        else outs() << "NULL";
        outs() << ")\n";
      } outs() << "}\n";
    }
}

double Points2_analysis::correct_freq(BasicBlock *ref, BasicBlock *bb, bool original)
{
  BasicBlock *call_bb = call_->getParent();

  outs() << "Correcting frequency for block: " << bb->getParent()->getName() << "/";
  print(ref);
  outs() << "/";
  print(bb);
  outs() << "\n";
  assert(call_ancestors_.count(bb) && "Trying to correct frequency of non predecessor block!");
  if (corrected_freqs_[ref].count(bb)) {
    outs() << "Found memoized correction: " << corrected_freqs_[ref][bb] << "\n";
    return corrected_freqs_[ref][bb]; // Memoized.
  }
  // If we got to call_'s block the frequency is bfreq(call_->getParent());
  // otherwise start as 0 and sum from successors' corrected frequency (DFS).
  if (bb == call_bb) return corrected_freqs_[ref][bb] = pass_.get_local_block_frequency(bb);
  else corrected_freqs_[ref][bb] = 0;
  for (BasicBlock *succ : successors(bb)) {
    outs() << "Successor: "; print(ref); outs() << "/"; print(succ); outs() << "\n";
    if (!(call_ancestors_.count(succ)) // The successor is not an ancestor to the call.
        || (function_map_.count(succ) && succ != ref) // The successor is a reference, but not the current one.
        || (function_map_[ref].count(succ)) // The successor overwrites the store in the current reference.
    ) { continue; } // Skip the successor if any of the coditions above were met.
    if (function_map_.count(succ)) { // We found a path ta reaches the call.
      outs() << "Path to call found\n";
      corrected_freqs_[ref][bb] += pass_.get_local_edge_frequency(bb, succ);
    } else { // DFS to correct successor.
      outs() << "DFS to successor\n";
      corrected_freqs_[ref][bb] += (pass_.get_local_edge_frequency(bb, succ) /  pass_.get_local_block_frequency(bb))
        * correct_freq(ref, succ, false);
    }
  }
  if (returns_.count(bb)) {// This block has a return, so it's callers are it's successors.
    outs() << "Return to [";
    print(returns_[bb]);
    outs() << "]\n";
    corrected_freqs_[ref][bb] += pass_.get_local_block_frequency(bb) * correct_freq(ref, returns_[bb], false);
  }
  if (original) corrected_freqs_[ref][bb] *= pass_.get_local_block_frequency(bb); //? / total_bfreq;
  return corrected_freqs_[ref][bb];// TODO: * ratio;
}
