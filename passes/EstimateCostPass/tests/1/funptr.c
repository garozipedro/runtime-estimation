typedef int (*funptr)(void);

int foo(void)
{
  return 1;
}

int bar(void)
{
  return 2;
}

int main(int argc)
{
  funptr fa = foo, fb = bar;
//  funptr fa, fb;

  if (argc < 3) {
    fa = foo;
    fb = bar;
  } else {
    fa = bar;
    fb = foo;
  }
  int result = fa() + fa() + fb();

  return result;
}
