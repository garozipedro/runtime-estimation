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

#include <set>
#include <unordered_map>
#include <vector>

#include "function_call_frequency_pass.hh"

using namespace llvm;
using namespace std;

cl::opt<bool> trace_indirect_calls(
  "trace-indirect-calls",
  cl::init(false),
  cl::desc("Trace calls through pointers. More costly but increases precision."),
  cl::value_desc("true or false")
);

/*
  Input:
  * A call graph, each node of which is a procedure and
  * each edge Fi -> Fj represents a call from function Fi to Fj.
  * Edge Fi -> Fj has local call frequency lfreq(Fi->Fj).

  Output:
  * Assignments of global function call frequency gfreq(Fi->Fj) to edge Fi -> Fj
  * and invocation frequency cfreq(F) to F.

  */
/* Algorithm steps:
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
            if (!call->getCalledFunction()) {// Can't directly determine the called function.
              if (trace_indirect_calls) {// If this improvement is enabled, add lfreqs from traced pointer calls.
                auto traced_functions = trace_indirect_call(call);
                for (auto &traced : traced_functions) {
                  outs() << "Traced call to function [" << traced.first->getName() << "] = "
                         << traced.second << "\n";
                  Edge edge = make_pair(&func, traced.first);
                  // TODO: sum all traced freqs and add them proportionally to call's BB bfreq.
                  lfreqs_[edge] = lfreqs_[edge] + // Add block's frequency to edge.
                    traced.second;
                  reachable_nodes.insert(traced.first);
                }
              } continue; // The classic algorithm doesn't resolve calls from pointers.
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
// Step 2. Filter the BBs from _CF_ that were originally from _SI_ and store them in :
//   for each si from _SI_:
//     function = si->getValueOperand();
//     result[function] = _CF_[si->getParent()];
vector<pair<llvm::Function *, double>> FunctionCallFrequencyPass::trace_indirect_call(CallInst *call)
{
  outs() << "***[ Tracing : " << *call << "]***\n";

  // Definitions.
  vector<pair<llvm::Function *, double>> result = {};
  BasicBlock *call_bb = call->getParent(); // _cbb_.
  set<BasicBlock *> call_preds = {}; // _CP_.
  map<BasicBlock *, StoreInst *> store_insts = {}; // _SI_.
  map<BasicBlock *, double> corrected_freqs = {}; // _CF_.
  double total_stores_freq = {}; // Frequency sum of all BB with relevant stores.

  // TODO: multimap.
  map<BasicBlock *, BasicBlock *> returns = {}; // Map BB with return to its caller.

  // Helper functions.
  function<void(set<BasicBlock *> &, BasicBlock *)> get_preds;
  function<Value *(CallInst*)> trace_return;
  //function<Value *(CallInst*)> trace_arg;
  //function<Value *(CallInst*)> trace_global;
  //function<Value *(CallInst*)> trace_field;

  {// Compute _CP_.
    get_preds = [&get_preds](set<BasicBlock *> &pred_set, BasicBlock *bb) -> void {
        for (auto pred : predecessors(bb))
          if (!pred_set.count(pred)) {
            pred_set.insert(pred);
            get_preds(pred_set, pred);
          }
      };
    call_preds.insert(call_bb);
    get_preds(call_preds, call_bb);

    // Print.
    outs() << "_CP_ = {";
    for (auto pred : call_preds) {
      outs() << " ";
      pred->printAsOperand(outs(), false);
    } outs() << " }\n";
  }
  {// Compute _SI_.
    outs() << "Computing _SI_:\n";
    function<void(Instruction *)> find_stores =
      [&](Instruction *val) {
        outs() << "Finding stores for [" << *val << "]\n";
        Value *mem = val;
        while (!dyn_cast<AllocaInst>(mem)) {// Find base memory reference.
          if (auto *load = dyn_cast<LoadInst>(mem)) {// Follow load instruction.
            mem = load->getPointerOperand();
          } else if (auto *call = dyn_cast<CallInst>(mem)) {
            mem = trace_return(call);
            // Add every BB from call's callee to call_preds.
            // TODO.
          }
          outs() << "\tReference: " << *mem << "\n";
        }
        // Follow stores to _mem_.
        outs() << "Stores to [" << *mem << "]\n";
        for (Value *user : mem->users()) {
          if (auto *store = dyn_cast<StoreInst>(user)) {
            if (!call_preds.count(store->getParent())) continue;
            outs() << "\t{" << *store << "}\n";
            Value *store_op = store->getValueOperand();
            if (auto *func = dyn_cast<Function>(store_op)) {
              bool cond =
                // The store must come before the Value is loaded.
                // TODO: preds[val].count(store->getParent()).
                (store->getParent() != val->getParent()) || (store->comesBefore(val)) &&
                // Previous stores in the same BB are overwritten.
                (!store_insts.count(store->getParent()) || store_insts[store->getParent()]->comesBefore(store));
              if (cond) store_insts[store->getParent()] = store;
            } else if (auto *inst = dyn_cast<Instruction>(store_op)) {
              outs() << "Following store\n";
              find_stores(inst);
            }
          }
        }
      };

    // Trace the value returned by call's callee.
    // E.g.
    //   define dso_local i32 @main() #0 {
    //     %1 = call ptr @getfun(ptr noundef @.str)
    //     %2 = call i32 %1(i32 noundef 0)
    //     ret i32 0
    //   }
    trace_return = [&](CallInst *call) -> Value * {
      outs() << "***[ trace_return() ]***\n";
      Function *callee = dyn_cast<Function>(call->getCalledOperand());
      Value *retval = nullptr;
      if (!callee) return nullptr;
      for (BasicBlock &bb : *callee) {
        call_preds.insert(&bb); // Every BB from called functions are predecessors to _call_.
        for (Instruction &inst : bb) {
          if (auto *ret = dyn_cast<ReturnInst>(&inst)) {
            assert(!retval && "Function has multiple returns!");
            retval = ret->getReturnValue();
            returns[&bb] = call->getParent();
          }
        }
      }
      return retval;
    };

    Instruction *callee = dyn_cast<Instruction>(call->getCalledOperand());
    outs() << "Callee = " << *callee << "\n";
    find_stores(callee);

    // Compute _total_stores_freq.
    for (auto it : store_insts) total_stores_freq += get_local_block_frequency(it.first);

    // Print.
    outs() << "_SI_ = (total = " << total_stores_freq << ") {\n";
    for (auto it : store_insts) {
      outs() << "\tbfreq(" << it.first->getParent()->getName() << "/";
      it.first->printAsOperand(outs(), false);  outs() << ") = " << get_local_block_frequency(it.first) << " // " << *it.second << "\n";
    } outs() << "}\n";
  }
  {// Compute _CF_.
    // Correct block frequencies using DFS.
    function<double(BasicBlock *)> correct_freq =
      [&](BasicBlock *bb) -> double {
        outs() << "Correcting frequency for block: ";
        bb->printAsOperand(outs(), false);
        outs() << "\n";
        assert(call_preds.count(bb) && "Trying to correct frequency of non predecessor block!");
        if (corrected_freqs.count(bb)) return corrected_freqs[bb]; // Memoized.
        corrected_freqs[bb] = bb == call_bb ? get_local_block_frequency(bb) : 0;
        for (BasicBlock *succ : successors(bb)) {
          if (!call_preds.count(succ) || store_insts.count(succ))
            continue; // Skip if its not a predecessor to <call_bb> or if it overwrites the store.
          if (succ == call_bb) { // We found a path ta reaches the call.
            corrected_freqs[bb] += get_local_edge_frequency(bb, succ);
          } else if (returns.count(succ)) {// The successor is a block with a return, so it's tied to his caller.
            corrected_freqs[bb] += correct_freq(returns[succ]);
          } else // DFS to correct successor.
            corrected_freqs[bb] += correct_freq(succ);
        }
        double ratio = get_local_block_frequency(bb) / total_stores_freq;
        outs() << "CORRECTED BB[" << bb->getParent()->getName() << "/"; bb->printAsOperand(outs(), false);
        outs() <<  "] "
               << "/ ratio = " << ratio
               << " / original freq = " << get_local_block_frequency(bb)
               << " / corrected freq = " << corrected_freqs[bb]
               << " / adjusted freq = " << corrected_freqs[bb] * ratio
               << "\n";
        return corrected_freqs[bb] * ratio;
      };
    for (auto it : store_insts) {
      correct_freq(it.first);
    }

    // Print.
    outs() << "Traced blocks ]====================\n";
    for (auto it : store_insts) {
      outs() << "\tBB ["; it.first->printAsOperand(outs(), false);
      outs() << "] // StoreInst: " << *it.second << "\n"
             << "\tFreq = " << get_local_block_frequency(it.first) << "\n";
    }
    outs() << "Corrected frequencies ]====================\n";
    for (auto it : store_insts) {
      outs() << "\tBB ["; it.first->printAsOperand(outs(), false);
      outs() << "] // StoreInst: " << *it.second << "\n"
             << "\tFreq = " << corrected_freqs[it.first] << "\n";
    }
  }

  // Push frequencies to <traced>.
  for (auto it : store_insts) {
    BasicBlock *store_bb = it.first;
    StoreInst *store_inst = it.second;
    double lfreq = corrected_freqs[store_bb];
    // If <store_inst> is storing a function Value, add it to <traced> (disregard store for nullptr assignments).
    if (Function *func = dyn_cast<Function>(store_inst->getValueOperand()))
      result.push_back(make_pair(func, lfreq));
  }

  return result;
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
