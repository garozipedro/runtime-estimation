#include <stdio.h>

typedef int (*fp)(void);

int zero(void)
{
  return 0;
}

int one(void)
{
  return 1;
}

fp getfun(int i)
{
  if (i % 2) return one;
  else return zero;
}

int main(int argc, char **argv)
{
  fp fa = getfun(0);
  if (argc) {
    fa = getfun(1);
  }
  fa();
  putchar('\n');
}
