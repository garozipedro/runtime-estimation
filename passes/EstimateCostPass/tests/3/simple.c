/*
int bar(int a)
{
  if (a < 0) return 1;
  if (a > 0) return -1;
  return 0;
}

int signal(int a)
{
  if (a > 0) return 1;
  if (a < 0) return -1;
  return 0;
}
*/

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
  return fb();
}
