#include <stdio.h>

int one(void)
{
  return 1;
}

int zero(void)
{
  return 0;
}

typedef int (*function)(void);

int main(int argc, char **argv)
{
  function fa = NULL, fb = NULL;
  for (int i = 0; i < argc; ++i) {
    if (i % 2) {
      fa = zero;
      fb = one;
    } else {
      fa = one;
      fb = zero;
    }
  }
  for (int i = 0; i < 10; ++i)
    printf("fa = %d // fb = %d\n", fa(), fb());
  return 0;
}
