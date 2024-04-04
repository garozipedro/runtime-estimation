#include <stdio.h>

typedef int (*fp)(void);
typedef fp (*get_fp)(void);

int zero(void) { return 0; }
int one(void) { return 1; }
fp get_zero(void) { return zero; }
fp get_one(void) { return one; }
get_fp get_fun(int i) { return (i % 2) ? get_one : get_zero; }

int main(int argc, char **argv)
{
  return get_fun(1)()();
  /* get_fp get = get_fun(1); */
  /* fp one_or_zero = get(); */
  /* return one_or_zero(); */
}
