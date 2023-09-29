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
#include <unordered_map>
#include <utility>
#include <vector>

#include "../WuLarus/A3.Function_call_frequency/function_call_frequency_pass.hh"

using namespace std;
using namespace llvm;

cl::opt<std::string> cost_opt(
  "prediction-cost-kind",
  cl::init("latency"), // cl::init("recipthroughput"), cl::init("codesize"), cl::init("sizeandlatency"),
  cl::desc("Specify cost kind used"),
  cl::value_desc("one or more of: recipthroughput, latency, codesize, sizeandlatency, one, dynamic")
);

struct EstimateCostPass : public PassInfoMixin<EstimateCostPass> {
  PreservedAnalyses run(Module &, ModuleAnalysisManager &);

private:
  FunctionCallFrequencyPass *wu_larus_ = nullptr;
};

llvm::PreservedAnalyses EstimateCostPass::run(llvm::Module &module, llvm::ModuleAnalysisManager &mam) {
  outs() << "Estimate Cost Pass for module:\n";
  outs() << module;

  outs() << "\n********************[ Running Wu & Larus ]********************\n\n";
  wu_larus_ = &mam.getResult<FunctionCallFrequencyPass>(module);
  outs() << "\n********************[ Ran Wu & Larus ]********************\n\n";
  for (Function &fun : module) {
    if (fun.empty()) continue;
    outs() << "Function [" << fun.getName() << "]\n";
    for (BasicBlock &bb : fun) {
      outs() << "\t[";  bb.printAsOperand(outs(), false);
      outs() << "] frequency = ("
//             << wu_larus_->getBlockEdgeFrequency(&fun)->getBlockFrequency(&bb)
             << wu_larus_->get_local_block_frequency(&bb) << ", "
             << wu_larus_->get_global_block_frequency(&bb) << ")\n";
    }
  }

  return llvm::PreservedAnalyses::all();
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
