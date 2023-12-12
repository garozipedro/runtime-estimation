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
  if (argc < 0) {
    fa = zero;
    fb = one;
  } else {
    fa = one;
    fb = zero;
  }
  if (fa()) {
    function aux = fa;
    fa = fb; fb = aux;
  }
  printf("fa = %d // fb = %d\n", fa(), fb());
  return 0;
}
