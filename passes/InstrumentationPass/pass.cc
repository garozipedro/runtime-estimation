#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <fstream>

using namespace llvm;
using namespace std;

// LLVM Context.
LLVMContext context;
Type *string_ty{ PointerType::getUnqual(Type::getInt8Ty(context)) };
Type *void_ty{ Type::getVoidTy(context) };
Type *int_ty{ Type::getInt64Ty(context) };
Value *zero{ ConstantInt::get(context, APInt(64, 0, true)) };

// Opt arguments.
//----------------------------------------------------------------------------------------------------------------------
cl::opt<std::string> instrumentation_granularity(
  "granularity",
  cl::init("basicblock"),
  cl::desc("Specify granularity of instrumentation placement (default = basicblock)."),
  cl::value_desc("one of: function, basicblock"));

cl::opt<std::string> instrumentation_output(
  "instrumentation-output",
  cl::init("instrumentation_output.txt"),
  cl::desc("Instrumentation output file name"));

cl::opt<std::string> yaml_output(
  "yaml-output",
  cl::init("yaml_output.yaml"),
  cl::desc("YAML output file name (histogram)"));

// InstrumentationPass.
//----------------------------------------------------------------------------------------------------------------------
// Program parameters specified through the command line.
enum class Granularity { BasicBlock, Function };
struct {
  Granularity granularity;
  const char *output_file, *yaml_file;
} params;

struct InstrumentationPass : public PassInfoMixin<InstrumentationPass> {
  PreservedAnalyses run(Module &mod, ModuleAnalysisManager &man) {
    module_ = &mod;
//    gen_info();

    // Create instrumentation functions.
    init_fun_ = Function::Create(
      FunctionType::get(int_ty, {string_ty}, false), Function::ExternalLinkage, "instrumentation_init", module_);
    finalize_fun_ = Function::Create(
      FunctionType::get(void_ty, false), Function::ExternalLinkage, "instrumentation_finalize", module_);
    start_fun_ = Function::Create(
      FunctionType::get(int_ty, {string_ty, int_ty}, false), Function::ExternalLinkage, "instrumentation_start", module_);
    resume_fun_ = Function::Create(
      FunctionType::get(int_ty, {string_ty, int_ty}, false), Function::ExternalLinkage, "instrumentation_resume", module_);
    stop_fun_ = Function::Create(
      FunctionType::get(int_ty, false), Function::ExternalLinkage, "instrumentation_stop", module_);
    pause_fun_ = Function::Create(
      FunctionType::get(int_ty, false), Function::ExternalLinkage, "instrumentation_pause", module_);

    // Set instrumentation output file (opened by instrumentation_init and closed by instrumentation_finalize).
    params.output_file = instrumentation_output.c_str();
    params.yaml_file = yaml_output.c_str();


    // Set instrumentation granularity.
    if (instrumentation_granularity == "basicblock") params.granularity = Granularity::BasicBlock;
    else if (instrumentation_granularity == "function") params.granularity = Granularity::Function;
    else {
      errs() << "Unrecognized granularity option: " << instrumentation_granularity << '\n';
      return PreservedAnalyses::all();
    }

    gen_yaml();
    instrument();

    return PreservedAnalyses::all();
  }

private:
  // Instrumentations.
  void gen_info();
  void gen_yaml();
  void instrument();

  // Helper functions.
  bool is_instrumentation_function(Function *call);
  bool can_instrument_function(Function &fun);
  Value *get_str_value(const char *str, IRBuilder<> &builder);
  void add_pause_or_resume(BasicBlock &bb, vector<Value *> &start_args, bool pause = true, bool resume = true);

  Module *module_;
  GlobalVariable *info_array_;
  // Instrumentation functions.
  Function *init_fun_, *finalize_fun_, *start_fun_, *stop_fun_, *resume_fun_, *pause_fun_;

};

// Generate array with block/function info.
//----------------------------------------------------------------------------------------------------------------------
void InstrumentationPass::gen_info()
{
  using Block_data = map<unsigned, unsigned long>; // Opcode -> count.
  using Function_data = map<BasicBlock *, Block_data>;
  using Instrumentation_data = map<Function *, Function_data>;

  // Gather BB/function info.
  Instrumentation_data data;
  for (Function &func : *module_)
    for (BasicBlock &bb : func)
      for (Instruction &instr : bb)
        if (params.granularity == Granularity::Function)
          data[&func][nullptr][instr.getOpcode()] += 1;
        else
          data[&func][&bb][instr.getOpcode()] += 1;

  // The number of functions.
  vector<Constant *> data_vec;
  data_vec.push_back(ConstantInt::get(int_ty, data.size(), false));
  for (auto &[fun, fdata] : data) {
    data_vec.push_back(ConstantInt::get(int_ty, reinterpret_cast<unsigned long>(fun), false));
    data_vec.push_back(ConstantInt::get(int_ty, fdata.size(), false));
    for (auto &[bb, bdata] : fdata) {
      data_vec.push_back(ConstantInt::get(int_ty, reinterpret_cast<unsigned long>(bb), false));
      data_vec.push_back(ConstantInt::get(int_ty, bdata.size(), false));
      for (auto [opcode, count] : bdata) {
        data_vec.push_back(ConstantInt::get(int_ty, opcode, false));
        data_vec.push_back(ConstantInt::get(int_ty, count, false));
      }
    }
  }

  ArrayType *type{ ArrayType::get(int_ty, data_vec.size()) };
  info_array_ = new GlobalVariable(
    *module_, type, true, GlobalValue::InternalLinkage,
    ConstantArray::get(type, data_vec), "___instrumentation_info___");

  info_array_->setAlignment(Align(16));
}

void InstrumentationPass::gen_yaml()
{
  using Block_data = map<unsigned, unsigned long>; // Opcode -> count.
  using Function_data = map<BasicBlock *, Block_data>;
  using Instrumentation_data = map<Function *, Function_data>;

  // Gather BB/function info.
  Instrumentation_data data;
  for (Function &func : *module_)
    for (BasicBlock &bb : func)
      for (Instruction &instr : bb)
        if (params.granularity == Granularity::Function)
          data[&func][nullptr][instr.getOpcode()] += 1;
        else
          data[&func][&bb][instr.getOpcode()] += 1;

  ofstream yaml{ params.yaml_file };
  if (!yaml.is_open()) {
    errs() << "Error: Unable to open file [" << params.yaml_file << "] for writing.\n";
    return;
  }
  yaml << "Instrumentation_data:\n";
  for (const auto &[fun, fdata] : data) {
    yaml << "  - Function:\n";
    yaml << "      Name: " << fun->getName().str() << '\n';
    yaml << "      BasicBlocks:\n";
    for (const auto &[bb, bdata] : fdata) {
      yaml << "        - BasicBlock:\n";
      yaml << "            ID: " << reinterpret_cast<uint64_t>(bb) << '\n';
      yaml << "            OpCodes:\n";
      for (const auto &[opcode, count] : bdata) {
        yaml << "              - " << opcode << ": " << count << '\n';
      }
    }
  }
  yaml.close();
}

// INSTRUMENTATION.
// * Insert start at the start of function/BB and after function call.
// * Insert stop at the end of function (at each return), at the end of BBs and before function calls.
//----------------------------------------------------------------------------------------------------------------------
void InstrumentationPass::instrument()
{
  // Instrument based on IR granularity selected.
  if (params.granularity == Granularity::Function) {
    for (Function &fun : *module_) {
      if (!can_instrument_function(fun)) continue;
      BasicBlock &first{ fun.front() }, &last{ fun.back() };
      IRBuilder<> builder{ &*first.getFirstInsertionPt() };
      Value *function_name{ get_str_value(fun.getName().data(), builder) };
      vector<Value *> start_args{ function_name, zero };

      builder.CreateCall(start_fun_, start_args, "instrumentation_start");
      builder.SetInsertPoint(last.getTerminator());
      builder.CreateCall(stop_fun_, None, "instrumentation_stop");
      for (BasicBlock &bb : fun) add_pause_or_resume(bb, start_args);
    }
  } else if (params.granularity == Granularity::BasicBlock) {
    for (Function &fun : *module_)
      for (BasicBlock &bb : fun) {
        IRBuilder<> builder{ &*bb.getFirstInsertionPt() };
        Value *function_name{ get_str_value(fun.getName().data(), builder) };
        vector<Value *> start_args{ function_name, builder.getInt64(reinterpret_cast<uint64_t>(&bb)) };

        // Instrument BB's entry and exit.
        builder.CreateCall(start_fun_, start_args, "instrumentation_start");
        builder.SetInsertPoint(bb.getTerminator());
        builder.CreateCall(stop_fun_, None, "instrumentation_stop");
        add_pause_or_resume(bb, start_args);
      }
  }
  if (Function *entry_fun{ module_->getFunction("main") }) {// Insert init/finalize at end of the main function.
    BasicBlock &first{ entry_fun->front() }, &last{ entry_fun->back() };
    IRBuilder<> builder{ &*first.getFirstInsertionPt() };
    vector<Value *> init_args{ get_str_value(params.output_file, builder) };

    builder.CreateCall(init_fun_, init_args);
    builder.SetInsertPoint(last.getTerminator());
    builder.CreateCall(finalize_fun_, None);
  }
}

// Helper functions.
//----------------------------------------------------------------------------------------------------------------------
bool InstrumentationPass::is_instrumentation_function(Function *call)
{
  return call == finalize_fun_ || call == start_fun_ || call == stop_fun_ || call == pause_fun_ || call == resume_fun_;
}

bool InstrumentationPass::can_instrument_function(Function &fun)
{
  return !(fun.empty() && !fun.isMaterializable());
}

Value *InstrumentationPass::get_str_value(const char *str, IRBuilder<> &builder)
{
  static unordered_map<const char *, Value *> str_ptrs{};
  Value *res;
  if (str_ptrs.count(str)) {
    res = str_ptrs[str];
  } else {
    res = str_ptrs[str] = builder.CreateGlobalStringPtr(str);
  }
  return res;
}

void InstrumentationPass::add_pause_or_resume(BasicBlock &bb, vector<Value *> &start_args, bool pause, bool resume)
{
  IRBuilder<> builder{&bb};
  auto it{ bb.begin() };
  while (it != bb.end()) {
    auto call{ dyn_cast<CallInst>(&*it) };
    if (call && !is_instrumentation_function(call->getCalledFunction())) {
      builder.SetInsertPoint(call);
      if (pause) builder.CreateCall(pause_fun_, None, "before_call");
      ++it;
      builder.SetInsertPoint(&*it);
      if (resume) builder.CreateCall(resume_fun_, start_args, "after_call");
    }
    ++it;
  }
}
// Register pass plugin.
//----------------------------------------------------------------------------------------------------------------------
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  static const char *pass_name{ "InstrumentationPass" };
  return {
    LLVM_PLUGIN_API_VERSION, pass_name,
    LLVM_VERSION_STRING,
    [](PassBuilder &pb) {
      pb.registerPipelineParsingCallback(
        [&](StringRef name, ModulePassManager &man, ArrayRef <PassBuilder::PipelineElement>) {
          if (name == pass_name) {
            man.addPass(InstrumentationPass{});
            return true;
          }
          return false;
        });
    }
  };
}
