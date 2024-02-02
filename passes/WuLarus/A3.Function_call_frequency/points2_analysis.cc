#include <variant>

/* Points-to analysis:
 * Improvement to allow Algorithm 3 use calls through pointers when counting local call frequecies.
***********************************************************************************************************************/
using GepInst = llvm::GetElementPtrInst;

using Ancestors = set<BasicBlock *>;
using Call_freq = pair<Function *, double>;
using Trace_map = map<BasicBlock *, vector<Call_freq>>;
using Bfreqs    = map<BasicBlock *, double>;

// Trace data.
//----------------------------------------------------------------------------------------------------------------------
enum class Arg_pos : int {trace_return = -1};
using Instruction_data = variant<monostate, Arg_pos>;
using Tinstr = pair<Instruction *, Instruction_data>;

#include "prints.cc"

struct Trace_data {
  Trace_data(Instruction *ref) : ref_{ref} { assert(ref); }

  Instruction *ref() const { return ref_; }
  Bfreqs &bfreqs() { return bfreqs_; }
  Trace_map &trace() { return trace_; }

  bool ok() {
    return ref_ && trace_.empty() && instructions_.empty() && ref_ancestors_.empty();
  }

  Tinstr get_instr() {
    if (instructions_.empty()) return{ nullptr, {} };
    auto result = instructions_.back();
    instructions_.pop_back();
    return result;
  }

  void push_instr(Instruction *instr) {
    assert(instr);
    instructions_.push_back({instr, {}});
  }

  void push_instr(Instruction *instr, Instruction_data data) {
    assert(instr);
    instructions_.push_back({instr, data});
  }

  bool has_instructions() const { return !instructions_.empty(); }

  // Get all predecessors of bb and predecessors of its predecessors up to the root.
  void add_ancestors(BasicBlock *bb) {
    ref_ancestors_.insert(bb);
    for (auto pred : predecessors(bb))
      if (!ref_ancestors_.count(pred)) {
        add_ancestors(pred);
      }
  }

  // bb is an ancestor of ref_.
  bool is_ancestor(BasicBlock *bb) const { return ref_ancestors_.count(bb); }

  // Check if bb is in trace.
  bool has_trace(BasicBlock *bb) const { return trace_.count(bb); }

  // Add Call_freq to BB.
  void add_cfreq(BasicBlock *bb, Call_freq cf) {
    trace_[bb].push_back(cf);
  }

  // Merge src trace to trace_[bb].
  void merge_trace(BasicBlock *bb, const Trace_data &src) {
    auto &dst = trace_[bb];
    auto &trace = src.trace_;
    for (const auto &[_, vec] : trace) // Merge all Call_freqs to dst.
      dst.insert(dst.end(), vec.begin(), vec.end());
  }

  // Sum all Call_freqs to function "foo()" together.
  void sum_trace(map<Function *, double> &dst) {
    for (const auto &[_, vec] : trace_) {
      for (const auto [func, freq] : vec) {
        if (func) dst[func] += freq;
      }
    }
  }

  friend raw_ostream &operator<<(raw_ostream &os, const Trace_data &data) {
    os << "Trace_data {\n"
       << "\t.ref = " << print(data.ref_) << '\n'
       << "\t.trace = {\n" << print(data.trace_) << "\t}\n"
       << "\t.instructions = {\n" << print(data.instructions_) << "\t}\n"
       << "\t.ancestors = {\n" << print(data.ref_ancestors_) << "\t}\n"
       << "}\n";
    return os;
  }

private:
  Instruction *ref_ = nullptr;
  deque<Tinstr> instructions_;
  Ancestors ref_ancestors_;
  Trace_map trace_;
  Bfreqs bfreqs_;
};

struct Points2_analysis {
  // The analysis is run by the constructor.
  Points2_analysis(FunctionCallFrequencyPass &pass, CallInst *call);

  // The functions that can be called by _call_ and their local call frequency.
  map<Function *, double> &get_result();

private:
  // Helper functions.

  void get_ancestors(Ancestors &ancestors, BasicBlock *bb);
  void merge_traces(vector<Call_freq> &dst, const Trace_map &src);

  // Map all basic blocks that may set the function called by call.
  void trace_main(Trace_data &data);
  void trace(Trace_data &data, AllocaInst *instr);
  void trace(Trace_data &data, LoadInst *instr);
  void trace(Trace_data &data, StoreInst *instr);
  void trace(Trace_data &data, CallInst *instr, Instruction_data idata);
  void trace(Trace_data &data, GepInst *instr);

  // Trace function params.
  void trace_args(Instruction *ref, CallInst *call, Trace_map &traced);

  // Correct block frequency when other successor block overwrites the its effects.
  double correct_freq(Trace_data &data, BasicBlock *bb, bool original);

  FunctionCallFrequencyPass &pass_; // We need local block and edge frequency info.
  CallInst *call_; // The indirect call instruction whose pointer operand we are mapping to actual functions.
  map<Function *, double> result_;
};

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
  debs << "\n************************************************************\n"
       << "***[ Tracing indirect call: " << print(call) << "]***"
       << "\n************************************************************\n";
  Trace_data data{ dyn_cast<Instruction>(call->getCalledOperand()) };
  trace_main(data);
  debs << "Final trace data:\n" << data;
  data.sum_trace(result_);
}

map<Function *, double> &Points2_analysis::get_result() { return result_; }

// Trace main.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace_main(Trace_data &data)
{
  assert(data.ok());
  data.push_instr(data.ref()); // Set ref as first instruction to be traced.
  data.add_ancestors(data.ref()->getParent());
  debs << "\nTracing [" << print(data.ref()) << "]\n";
  debs << "************************************************************\n";
  while (data.has_instructions()) {
    debs << data;
    auto [instr, idata] = data.get_instr();
    BasicBlock  *instr_bb = instr->getParent();
    debs << "Current instruction = [" << print(instr) << "]\n";
    switch (instr->getOpcode()) {
    case Instruction::Alloca:        trace(data, dyn_cast<AllocaInst>(instr)); break;
    case Instruction::Load:          trace(data, dyn_cast<LoadInst  >(instr)); break;
    case Instruction::Store:         trace(data, dyn_cast<StoreInst >(instr)); break;
    case Instruction::Call:          trace(data, dyn_cast<CallInst  >(instr), idata); break;
    case Instruction::GetElementPtr: trace(data, dyn_cast<GepInst   >(instr)); break;
    default:
      errs() << "Couldn't handle instruction: " << instr->getOpcode() << " (" << instr->getOpcodeName() << ")\n";
      abort();
    }
  }
  Bfreqs bfreqs{}; // Used for memoization by correct_freq.
  for (auto &[bb, vec] : data.trace()) {// Correct the frequency of each block that has call freqs.
    double corrected_freq = correct_freq(data, bb, true);
    for (auto &[_, freq] : vec) freq *= corrected_freq;
  }
}

// Trace alloca.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, AllocaInst *alloca)
{ assert(alloca);
  map<BasicBlock *, StoreInst *> stores{}; // Keep only the last StoreInst per BasicBlock.
  debs << "TRACING ALLOCA\n";
  for (User *user : alloca->users()) {// Get all stores to this alloca.
    debs << "USER: " << *user << '\n';
    if (auto store{ dyn_cast<StoreInst>(user) }) {
      BasicBlock *store_bb{ store->getParent() };
      if (// Skip this store given any of the following conditions:
        (store_bb == data.ref()->getParent() && !store->comesBefore(data.ref())) // Store after use.
        || !data.is_ancestor(store_bb) // Store is not in an ancestor block.
      ) continue;

      // NOTE: here we rely on the fact that llvm user comes in reverse use order (last user is inserted in stores,
      // so we don't have to check if another store overwrites it).
      if (!stores.count(store_bb)) {
        debs << "Pushing store: " << print(store) << "\n";
        stores[store_bb] = store;
        data.push_instr(store);
      }
    }
  }
}

// Trace load.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, LoadInst *load)
{ assert(load);
  debs << "TRACING LOAD\n";
  if (load != data.ref()) {// Change of reference instruction.
    debs << "Tracing new reference load\n";
    Trace_data load_data{ load };
    trace_main(load_data);
    data.merge_trace(load->getParent(), load_data);
  } else {
    debs << "Pushing Load operand [" << *load->getPointerOperand() << "]\n";
    data.push_instr(dyn_cast<Instruction>(load->getPointerOperand()));
  }
}

// Trace store.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, StoreInst *store)
{ assert(store);
  debs << "TRACING STORE\n";
  if (CallInst *call = dyn_cast<CallInst>(store->getValueOperand())) {// We must be storing the return of the call.
    debs << "Pushing call: " << print(call) << '\n';
    data.push_instr(call, Arg_pos::trace_return);
  } else if (Instruction *instr = dyn_cast<Instruction>(store->getValueOperand())) {// We need to trace the store operand.
    debs << "Pushing operand: " << print(instr) << '\n';
    data.push_instr(instr);
  } else {// The store is a final value (a function ptr or null).
    auto func{ dyn_cast<Function>(store->getValueOperand()) };
    data.add_cfreq(store->getParent(), {func, 1});
  }
}

// Trace call.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, CallInst *call, Instruction_data idata)
{ assert(call);
  debs << "TRACING CALL: " << print(call) << '\n';
  if (get<Arg_pos>(idata) == Arg_pos::trace_return) {
    debs << "Tracing return\n";
    if (auto callee{ dyn_cast<Function>(call->getCalledOperand()) }) {
      BasicBlock &last_bb{ callee->back() };
      auto ret{ dyn_cast<ReturnInst>(callee->back().getTerminator()) };
      assert(ret && "BasicBlock terminator is not a return");
      if (auto instr{ dyn_cast<Instruction>(ret->getReturnValue()) }) {// The return is an instruction.
        debs << "Pushing function's return operand: " << print(instr) << '\n';
        Trace_data call_data{ instr };
        trace_main(call_data);
        data.merge_trace(call->getParent(), call_data);
      } else if (auto func{ dyn_cast<Function>(ret->getReturnValue()) }) {// The return is final.
        debs << "Pushing function's return value: " << print(func) << '\n';
        data.add_cfreq(call->getParent(), {func, 1});
      }
    }
  } else if (int arg_pos{ static_cast<int>(get<Arg_pos>(idata)) }) {
    debs << "Tracing argument: " << arg_pos << '\n';
  }
}

// Trace gep.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, GetElementPtrInst *gep)
{ assert(gep);
  debs << "TRACING GEP\n";
}

// Correct freq.
//----------------------------------------------------------------------------------------------------------------------
double Points2_analysis::correct_freq(Trace_data &data, BasicBlock *bb, bool original)
{
  debs << "\nCORRECT_FREQ: " << print(data.ref()) << "//" << print(bb) << "//" << original << '\n';
  assert(data.is_ancestor(bb) && "Trying to correct frequency of non predecessor block!");

  if (data.bfreqs().count(bb)) {
    debs << "Found memoized correction: " << data.bfreqs()[bb] << "\n";
    return data.bfreqs()[bb]; // Memoized.
  }
  // If we got to ref, the frequency is bfreq(bb);
  // otherwise start as 0 and sum from successors' corrected frequency (DFS).
  if (bb == data.ref()->getParent()) return data.bfreqs()[bb] = 1;
  else data.bfreqs()[bb] = 0;
  for (BasicBlock *succ : successors(bb)) {
    debs << "Successor: " << print(bb) << "//" << print(succ) << '\n';
    if (// Skip the successor if any of the coditions are met.
      !(data.is_ancestor(succ)) // The successor is not an ancestor to ref.
      || (data.has_trace(succ)) // The successor overwrites bb effect.
    ) continue;
    debs << "DFS to successor\n";
    data.bfreqs()[bb] += (pass_.get_local_edge_frequency(bb, succ) /  pass_.get_local_block_frequency(bb))
      * correct_freq(data, succ, false);
  }
  if (original) data.bfreqs()[bb] *= pass_.get_local_block_frequency(bb);
  return data.bfreqs()[bb];
}
