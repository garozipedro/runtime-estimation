#include <string.h>

typedef int (*fp)(int);

int foo(int a)
{
  return 7;
}

int bar(int a)
{
  return 42;
}

fp getfun(char *name)
{
  if (!strcmp(name, "foo")) return foo;
  if (!strcmp(name, "bar")) return bar;
  return NULL;
}

int main()
{
  return getfun("foo")(0);
}
