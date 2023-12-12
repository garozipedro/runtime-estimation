struct My_base_class {
  virtual int foo() { return 1; };
};

struct My_derived_class : My_base_class {
  virtual int foo() { return 2; };
  virtual int foo(int i) { return i + 1; };
};

int main() {
  My_derived_class *p = new My_derived_class;
  int result1 = p->foo(1);
  int result2 = p->foo();
  My_base_class* q = p;
  int result3 = q->foo();
}
