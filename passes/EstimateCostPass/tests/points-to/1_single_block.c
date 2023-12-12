int foo(void)
{
  return 42;
}

int bar(void)
{
  return 13;
}

int main()
{
  int (*fa)(void) = foo;
  fa = bar;
  int (*fb)(void) = fa;
  fa = foo;
  int (*fc)(void) = fa;
  fa = bar;
  fc = fa;
  int (*fd)(void) = foo;
  fa = foo;
  fd = fc;

  return fd();
}
