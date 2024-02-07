string print(Function *func)
{
  string str;
  raw_string_ostream ss{str};
  ss << "[";
  if (func) ss << string{func->getName()};
  else ss << "NULL";
  ss << "]";
  return str;
}

string print(BasicBlock *bb)
{
  string str;
  raw_string_ostream ss{str};

  ss << print(bb->getParent());
  ss << "::[";
  bb->printAsOperand(ss, false);
  ss << "]";
  return str;
}

string print(Instruction *instr)
{
  string str;
  raw_string_ostream ss{str};
  ss << print(instr->getParent());
  ss << "::[" << *instr << "]";
  return str;
}

string print(const Trace_map &tm)
{
  string str;
  raw_string_ostream ss{str};
  for (auto &it : tm) {
    ss << print(it.first) << " may point to:\n";
    for (auto &call : it.second) {
      ss << print(call.first) << " = " << call.second << "\n";
    }
  }
  return str;
}

string print(const deque<Tinstr> &instrs)
{
  string str;
  raw_string_ostream ss{str};
  for (auto [instr, _] : instrs) {
    ss << "=>" << print(instr) << '\n';
  }
  return str;
}

string print(const Ancestors &ancestors)
{
  string str;
  raw_string_ostream ss{str};
  for (auto &it : ancestors) {
    ss << "=>" << print(it) << '\n';
  }
  return str;
}

string print(const Bfreqs &bfreqs)
{
  string str;
  raw_string_ostream ss{str};
  for (auto [bb, freq] : bfreqs) {
    ss << print(bb) << " = " << freq << '\n';
  }
  return str;
}
