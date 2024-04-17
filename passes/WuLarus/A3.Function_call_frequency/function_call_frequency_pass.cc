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
#include <sstream>
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

#ifndef NDEBUG
raw_ostream &debs = outs();
#else
raw_ostream &debs = nulls();
#endif

#include "points2_analysis.cc"

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
  Points2_analysis p2 (*this);
  int p2_count{ 0 };

//  debs << module;
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
            // Can't directly determine the called function, use points-to analysis.
            if (!call->getCalledFunction() && use_points2) {
              auto traced_functions{ p2.run(call) };
              outs() << "Traced " << ++p2_count << " functions\n";
              for (auto &traced : traced_functions) {
                Edge edge = make_pair(&func, traced.first);
                lfreqs_[edge] = lfreqs_[edge] + // Add block's frequency to edge.
                  traced.second;
                reachable_nodes.insert(traced.first);
              }
            } else {
              Edge edge = make_pair(&func, call->getCalledFunction());
              lfreqs_[edge] = lfreqs_[edge] + // Add block's frequency to edge.
                getBlockEdgeFrequency(&func)->getBlockFrequency(&bb);
              reachable_nodes.insert(call->getCalledFunction());
            }
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
//      jt->second = !(reachable.find(jt->first) != reachable.end());
      jt->second = false;
    }
    visited_functions_[entry_func] = false;
  }
  {// Step.4.
    propagate_call_freq(entry_func, entry_func, true);
    // TODO: update gfreq.
  }
  // Sum incoming cfreqs for functions not propagated to.
  // for (Function &func : module) {
  //   if (cfreqs_.find(&func) == cfreqs_.end()) {
  //     for (auto reachables : reachable_functions_) {
  //       if (&func != reachables.first && (reachables.second.find(&func) != reachables.second.end())) {
  //         // reachables.first is a precedessor of func.
  //         Edge edge = make_pair(reachables.first, &func);
  //         cfreqs_[&func] += gfreqs_[edge];
  //       }
  //     }
  //   }
  // }
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
      else if (back_edges_.find(fp_f) == back_edges_.end()) {
        cfreqs_[f] += gfreqs_[fp_f];
      }
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
