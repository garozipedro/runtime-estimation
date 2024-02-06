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
enum class Arg_pos : int { trace_return = -1 };
enum class Trace_dir { regular, reverse };
using Instruction_data = variant<monostate, Arg_pos, Trace_dir>;
using Tinstr = pair<Instruction *, Instruction_data>;

#include "prints.cc"

struct Trace_data {
  Trace_data(Instruction *ref) : ref_{ref}, first_instr_{ref} { assert(ref); }
  Trace_data(Instruction *ref, Instruction *first) : ref_{ref}, first_instr_{first} { assert(ref && first); }

  Instruction *ref() const { return ref_; }
  Instruction *first_instr() const { return first_instr_; }
  Bfreqs &bfreqs() { return bfreqs_; }
  Trace_map &trace() { return trace_; }

  bool ok() {
    return ref_ && trace_.empty() && instructions_.empty() && ref_ancestors_.empty();
  }

  Tinstr get_instr() {
    if (instructions_.empty()) return{ nullptr, {} };
    auto result{ instructions_.back() };
    instructions_.pop_back();
    return result;
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
    auto &dst{ trace_[bb] };
    auto &trace{ src.trace_ };
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
  Instruction *ref_, *first_instr_;
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
  void trace_main(Trace_data &data, Instruction_data idata);
  void trace(Trace_data &data, AllocaInst *instr, Instruction_data idata);
  void trace(Trace_data &data, LoadInst *instr, Instruction_data idata);
  void trace(Trace_data &data, StoreInst *instr, Instruction_data idata);
  void trace(Trace_data &data, CallInst *instr, Instruction_data idata);
  void trace(Trace_data &data, Argument *arg, Instruction_data idata);
  void trace(Trace_data &data, GepInst *instr, Instruction_data idata);

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
  trace_main(data, Trace_dir::regular);
  debs << "Final trace data:\n" << data;
  data.sum_trace(result_);
}

map<Function *, double> &Points2_analysis::get_result() { return result_; }

// Helper functions.
//----------------------------------------------------------------------------------------------------------------------
static bool same_gep_indices(GepInst *a, GepInst *b)
{
  auto ait{ a->idx_begin() };
  auto bit{ b->idx_begin() };
  do {
    if (*ait != *bit) return false;
  } while ((++ait != a->idx_end()) && (++bit != b->idx_end())) ;
  return true;
}

// Trace main.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace_main(Trace_data &data, Instruction_data idata)
{
  assert(data.ok());
  data.push_instr(data.first_instr(), idata);
  data.add_ancestors(data.ref()->getParent());
  debs << "\nTracing [" << print(data.ref()) << "]\n";
  debs << "************************************************************\n";
  while (data.has_instructions()) {
    debs << data;
    auto [instr, idata]{ data.get_instr() };
    BasicBlock  *instr_bb{ instr->getParent() };
    debs << "Current instruction = [" << print(instr) << "]\n";
    switch (instr->getOpcode()) {
    case Instruction::Alloca:        trace(data, dyn_cast<AllocaInst>(instr), idata); break;
    case Instruction::Load:          trace(data, dyn_cast<LoadInst  >(instr), idata); break;
    case Instruction::Store:         trace(data, dyn_cast<StoreInst >(instr), idata); break;
    case Instruction::Call:          trace(data, dyn_cast<CallInst  >(instr), idata); break;
    case Instruction::GetElementPtr: trace(data, dyn_cast<GepInst   >(instr), idata); break;
    default:
      errs() << "Couldn't handle instruction: " << instr->getOpcode() << " (" << instr->getOpcodeName() << ")\n";
      abort();
    }
  }
  Bfreqs bfreqs{}; // Used for memoization by correct_freq.
  for (auto &[bb, vec] : data.trace()) {// Correct the frequency of each block that has call freqs.
    double corrected_freq{ correct_freq(data, bb, true) };
    for (auto &[_, freq] : vec) freq *= corrected_freq;
  }
}

// Trace alloca.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, AllocaInst *alloca, Instruction_data idata)
{ assert(alloca);
  if (get<Trace_dir>(idata) == Trace_dir::regular) {// TODO: calls may not set their argument.
    map<BasicBlock *, StoreInst *> stores{}; // Keep only the last StoreInst per BasicBlock.
    debs << "TRACING ALLOCA: regular\n";
    for (User *user : alloca->users()) {// Get all stores to this alloca.
      debs << "USER: " << *user << '\n';
      if (auto store{ dyn_cast<StoreInst>(user) }) {// Handle stores to alloca.
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
          data.push_instr(store, Trace_dir::regular);
        }
      } else if (auto call{ dyn_cast<CallInst>(user) }) {// Handle calls with alloca as argument.
        Arg_pos pos{ static_cast<int>(distance(call->arg_begin(), find(call->arg_begin(), call->arg_end(), alloca))) };
        debs << "Pushing call: " << print(call) << " with arg: " << static_cast<int>(pos) << '\n';
        data.push_instr(call, pos);
      }
    }
  } else {// Reverse
    debs << "TRACING ALLOCA: reverse\n";
    for (User *user : alloca->users()) {// Get all stores to this alloca.
      debs << "USER: " << *user << '\n';
      if (auto load{ dyn_cast<LoadInst>(user) }) // Handle loads from alloca.
        data.push_instr(load, Trace_dir::reverse);
    }
  }
}

// Trace load.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, LoadInst *load, Instruction_data idata)
{ assert(load);
  if (get<Trace_dir>(idata) == Trace_dir::regular) {
    debs << "TRACING LOAD: regular\n";
    if (load != data.ref()) {// Change of reference instruction.
      debs << "Tracing new reference load\n";
      Trace_data load_data{ load };
      trace_main(load_data, Trace_dir::regular);
      data.merge_trace(load->getParent(), load_data);
    } else {
      debs << "Pushing Load operand [" << *load->getPointerOperand() << "]\n";
      data.push_instr(dyn_cast<Instruction>(load->getPointerOperand()), Trace_dir::regular);
    }
  } else {// Reverse.
    for (User *user : load->users()) {
      debs << "USER: " << *user << '\n';
      if (auto store{ dyn_cast<StoreInst>(user) }) {
        data.push_instr(store, Trace_dir::regular);
      } else if (auto call{ dyn_cast<CallInst>(user) }) {
        Arg_pos pos{ static_cast<int>(distance(call->arg_begin(), find(call->arg_begin(), call->arg_end(), load))) };
        debs << "Pushing call: " << print(call) << " with arg: " << static_cast<int>(pos) << '\n';
        data.push_instr(call, pos);
      }
    }
  }
}

// Trace store.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, StoreInst *store, Instruction_data idata)
{ assert(store);
  if (get<Trace_dir>(idata) == Trace_dir::regular) {
    debs << "TRACING STORE: regular\n";
    if (auto call{ dyn_cast<CallInst>(store->getValueOperand()) }) {// We must be storing the return of the call.
      debs << "Pushing call: " << print(call) << '\n';
      data.push_instr(call, Arg_pos::trace_return);
    } else if (auto instr{ dyn_cast<Instruction>(store->getValueOperand()) }) {// We need to trace the store operand.
      debs << "Pushing operand: " << print(instr) << '\n';
      data.push_instr(instr, Trace_dir::regular);
    } else {// The store is a final value (a function ptr or null).
      auto func{ dyn_cast<Function>(store->getValueOperand()) };
      debs << "Pushing final value: " << print(func) << '\n';
      data.add_cfreq(store->getParent(), {func, 1});
    }
  } else {// Reverse.
    debs << "TRACING STORE: reverse\n";
    if (auto instr{ dyn_cast<Instruction>(store->getPointerOperand()) }) {
      data.push_instr(instr, Trace_dir::reverse);
    } else {
      errs() << "Non instr pointer operand?\n";
      abort();
    }
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
      auto ret{ dyn_cast<ReturnInst>(callee->back().getTerminator()) };
      assert(ret && "BasicBlock terminator is not a return");
      if (auto instr{ dyn_cast<Instruction>(ret->getReturnValue()) }) {// The return is an instruction.
        debs << "Pushing function's return operand: " << print(instr) << '\n';
        Trace_data call_data{ instr };
        trace_main(call_data, Trace_dir::regular);
        data.merge_trace(call->getParent(), call_data);
      } else if (auto func{ dyn_cast<Function>(ret->getReturnValue()) }) {// The return is final.
        debs << "Pushing function's return value: " << print(func) << '\n';
        data.add_cfreq(call->getParent(), {func, 1});
      }
    } else {// TODO: indirect call.
      abort();
    }
  } else {// Trace argument.
    debs << "Tracing function argument\n";
    Function *func{ call->getCalledFunction() };
    Argument *arg{ func->getArg(static_cast<int>(get<Arg_pos>(idata))) };
    for (User *user : arg->users()) {
      debs << "User: " << *user << '\n';
      if (auto store{ dyn_cast<StoreInst>(user) }) {
        debs << "STORE\n";
        Trace_data call_data{ func->back().getTerminator(), store };
        trace_main(call_data, Trace_dir::reverse);
        data.merge_trace(call->getParent(), call_data);
      }
    }
  }
}

// Trace gep.
//----------------------------------------------------------------------------------------------------------------------
void Points2_analysis::trace(Trace_data &data, GetElementPtrInst *gep, Instruction_data idata)
{ assert(gep);
  if (get<Trace_dir>(idata) == Trace_dir::regular) {
    debs << "TRACING GEP regular\n";
    Type *gep_type{ gep->getSourceElementType() };
    debs << "Got a gep: " << *gep
         << "\n\t->" << *gep->getSourceElementType() << " // "
         << "\n\t->" << *gep->getResultElementType() << "//"
         << "\n\t->" << *gep->getPointerOperand() << "//"
         << "\n\t->" << *gep->getPointerOperandType() << "\n";
    for (auto &idx : gep->indices()) {
      debs << "\tIdx: " << *idx << '\n';
    }
    if (auto *ty{ dyn_cast<StructType>(gep_type) }) {
      debs << "Got Struct GEP\n";
      for (User *user : gep->getPointerOperand()->users()) {
        auto ugep{ dyn_cast<GepInst>(user) };
        if (same_gep_indices(ugep, gep)) {// Both access the same field.
          bool same_block{ ugep->getParent() == gep->getParent() };
          if ((same_block && ugep->comesBefore(gep))
              || (!same_block && data.is_ancestor(ugep->getParent()))) {
            debs << "USER: " << *ugep << '\n';
            data.push_instr(ugep, Trace_dir::reverse);
          }
        }
      }
    } else if (auto *ty{ dyn_cast<ArrayType>(gep_type) }) {
      debs << "Got Array GEP\n";
    } else if (auto *ty{ dyn_cast<PointerType>(gep_type) }) {
      debs << "Got Pointer GEP\n";
    } else {
      errs() << "Couldn't handle gep's source element type: " << *gep_type << '\n';
      abort();
    }
  } else {// Reverse.
    debs << "TRACING GEP reverse\n";
    Type *gep_type{ gep->getSourceElementType() };
    debs << "Got a gep: " << *gep
         << "\n\t->" << *gep->getSourceElementType() << " // "
         << "\n\t->" << *gep->getResultElementType() << "//"
         << "\n\t->" << *gep->getPointerOperand() << "//"
         << "\n\t->" << *gep->getPointerOperandType() << "\n";
    for (auto &idx : gep->indices()) {
      debs << "\tIdx: " << *idx << '\n';
    }
    for (User *user : gep->users()) {
      debs << "USER: " << *user << '\n';
      if (auto store{ dyn_cast<StoreInst>(user) }) {
        data.push_instr(store, Trace_dir::regular);
      }
    }
  }
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
