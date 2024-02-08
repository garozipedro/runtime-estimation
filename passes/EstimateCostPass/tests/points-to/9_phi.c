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
  function foo;
  if (argc > 10) {
    foo = zero;
  } else if (argc > 5) {
    foo = one;
  } else {
    foo = 0;
  }
  return foo();
}
