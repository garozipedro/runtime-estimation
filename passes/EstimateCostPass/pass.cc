/*
  This file is distributed under the University of Illinois Open Source
  License. See LICENSE for details.
*/

#include <llvm/ADT/SmallSet.h>
#include <llvm/Analysis/BlockFrequencyInfo.h>
#include <llvm/Analysis/BlockFrequencyInfoImpl.h>
#include <llvm/Analysis/BranchProbabilityInfo.h>
#include <llvm/Analysis/BranchProbabilityInfo.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Compiler.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../WuLarus/A3.Function_call_frequency/function_call_frequency_pass.hh"

using namespace std;
using namespace llvm;

cl::opt<std::string> arg_cost_opt(
  "prediction-cost-kind",
  cl::init("latency"), // cl::init("recipthroughput"), cl::init("codesize"), cl::init("sizeandlatency"),
  cl::desc("Specify cost kind used"),
  cl::value_desc("one or more of: recipthroughput, latency, codesize, sizeandlatency, one, dynamic")
);

enum class Cost_option {
  latency, recipthroughput, codesize, sizeandlatency, one, dynamic,
};

const char *cost_name(Cost_option cost)
{
  switch (cost) {
  case Cost_option::latency: return "Latency";
  case Cost_option::recipthroughput: return "Recipthroughput";
  case Cost_option::codesize: return "Codesize";
  case Cost_option::sizeandlatency: return "Sizeandlatency";
  case Cost_option::one: return "One";
  case Cost_option::dynamic: return "Dynamic";
  default: return "";
  }
}

bool is_llvm_cost(Cost_option cost)
{
  switch (cost) {
  case Cost_option::latency: case Cost_option::recipthroughput:
  case Cost_option::codesize: case Cost_option::sizeandlatency:
    return true;
  default: return false;
  }
}

TargetTransformInfo::TargetCostKind cost_opt_to_tti_cost(Cost_option cost)
{
  switch (cost) {
  case Cost_option::latency: return TargetTransformInfo::TargetCostKind::TCK_Latency;
  case Cost_option::recipthroughput: return TargetTransformInfo::TargetCostKind::TCK_RecipThroughput;
  case Cost_option::codesize: return TargetTransformInfo::TargetCostKind::TCK_CodeSize;
  case Cost_option::sizeandlatency: return TargetTransformInfo::TargetCostKind::TCK_SizeAndLatency;
  default: assert(false);
  }
}

struct EstimateCostPass : public PassInfoMixin<EstimateCostPass> {
  PreservedAnalyses run(Module &, ModuleAnalysisManager &);

private:
  void select_costs();
  void print_freqs(Module &);
  void compute_cost(Module &);
  void compute_cost(Function &);
  void compute_cost(BasicBlock &, TargetTransformInfo *);
  void generate_yaml();

  map<Cost_option, map<Function *, double>> costs_{};
  bool llvm_cost_selected_{ false };

  FunctionCallFrequencyPass *wu_larus_ = nullptr;
  FunctionAnalysisManager *fam_;
};

llvm::PreservedAnalyses EstimateCostPass::run(llvm::Module &module, llvm::ModuleAnalysisManager &mam) {
//-  outs() << "Estimate Cost Pass for module: [" << module.getName() << "]\n";;
  fam_ = &mam.getResult<FunctionAnalysisManagerModuleProxy>(module).getManager();

//  outs() << "\n\n********************[ Running Wu & Larus ]********************\n";
  wu_larus_ = &mam.getResult<FunctionCallFrequencyPass>(module);
//  outs() << "\n\n********************[ Ran Wu & Larus ]********************\n";
  // Multiply frequencies by instruction costs.
  select_costs();
  compute_cost(module);
  generate_yaml();
  return llvm::PreservedAnalyses::all();
}

void EstimateCostPass::select_costs()
{
  stringstream ss{ arg_cost_opt };
  string cost{};
  costs_.clear();
  while (getline(ss, cost, ',')) {
    if (cost == "latency") { costs_[Cost_option::latency] = {}; llvm_cost_selected_ = true; }
    else if (cost == "recipthroughput") { costs_[Cost_option::recipthroughput] = {}; llvm_cost_selected_ = true; }
    else if (cost == "codesize") { costs_[Cost_option::codesize] = {}; llvm_cost_selected_ = true; }
    else if (cost == "sizeandlatency") { costs_[Cost_option::sizeandlatency] = {}; llvm_cost_selected_ = true; }
    else if (cost == "one") costs_[Cost_option::one] = {};
    else if (cost == "dynamic") costs_[Cost_option::dynamic] = {};
    else errs() << "Unrecognized cost kind [" << cost << "]\n";
  }
}

void EstimateCostPass::compute_cost(Module &mod)
{
  for (Function &fun: mod)
    compute_cost(fun);
}

void EstimateCostPass::compute_cost(Function &fun)
{
  // if (granularity == function) ...
  for (BasicBlock &bb: fun)
    compute_cost(bb, llvm_cost_selected_ ? &fam_->getResult<TargetIRAnalysis>(*bb.getParent()) : nullptr);
}

void EstimateCostPass::compute_cost(BasicBlock &bb, TargetTransformInfo *tti)
{
  Function *fun = bb.getParent();
  double cos{ 0 };
  double freq{ wu_larus_->get_global_block_frequency(&bb) };
  for (auto &[cost_opt, _] : costs_) {
    // Default LLVM costs from TargetIRAnalysis.
    if (cost_opt == Cost_option::one) {
      costs_[cost_opt][fun] += bb.size() * freq;
    } else if (cost_opt == Cost_option::dynamic) {
      // TODO.
    } else if (is_llvm_cost(cost_opt)) {
      for (Instruction &instr : bb) {
        auto tti_cost{ tti->getInstructionCost(&instr, cost_opt_to_tti_cost(cost_opt)).getValue() };
        double icost{ tti_cost.hasValue() ? static_cast<double>(tti_cost.getValue()) : 0 };
        costs_[cost_opt][fun] += icost * freq;
      }
    }
  }
}

void EstimateCostPass::print_freqs(Module &module)
{
  for (Function &fun : module) {
    if (fun.empty()) continue;
    outs() << "Function [" << fun.getName() << "]\n";
    for (BasicBlock &bb : fun) {
      outs() << "\t[";  bb.printAsOperand(outs(), false);
      outs() << "] frequency = ("
             << wu_larus_->get_local_block_frequency(&bb) << ", "
             << wu_larus_->get_global_block_frequency(&bb) << ")\n";
      for (BasicBlock *succ : successors(&bb)) {
        outs() << "\t->[";
        succ->printAsOperand(outs(), false);
        outs() << "] = " << wu_larus_->get_local_edge_frequency(&bb, succ) << "\n";
      }
    }
  }
}

void EstimateCostPass::generate_yaml()
{
  outs() << "Cost_options:\n";
  for (auto &[cost_option, function_cost] : costs_) {
    double program_cost = 0;
    outs() << "- Option:\n";
    outs() << "    Name: " << cost_name(cost_option) << '\n';
    outs() << "    Functions:\n";
    for (auto &[function, cost] : function_cost) {
      outs() << "    - Function:\n"
             << "        Name: " << function->getName() << '\n'
             << "        Cost: " << cost << '\n';
      program_cost += cost;
    }
    outs() << "    Total cost: " << program_cost << '\n';
  }
}

extern "C" LLVM_ATTRIBUTE_WEAK llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "EstimateCostPass",
    LLVM_VERSION_STRING,
    [](llvm::PassBuilder &pb) {
      pb.registerPipelineParsingCallback(
        [&](llvm::StringRef name, llvm::ModulePassManager &mam, llvm::ArrayRef <llvm::PassBuilder::PipelineElement>) {
          if (name == "EstimateCostPass") {
            mam.addPass(EstimateCostPass());
            return true;
          }
          return false;
        });
    }
  };
}
