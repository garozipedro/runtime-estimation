#include <stdio.h>

int even(int n);
int odd(int n);

int even(int n)
{
  if (n == 0) return 1;
  return odd(n - 1);
}

int odd(int n)
{
  if (n == 0) return 0;
  return even(n - 1);
}

int main()
{
  int d;
  scanf("%d", &d);
  printf("%d is %s\n", d, even(d) ? "even" : "odd");
}
