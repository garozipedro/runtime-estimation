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
  function foo = argc > 1 ? one : zero;
  return foo();
}
