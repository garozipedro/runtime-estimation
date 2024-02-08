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
  switch (argc) {
  case 1: foo = zero; break;
  case 2: foo = one; break;
  case 3: foo = one; break;
  case 4: foo = zero; break;
  case 5: foo = one; break;
  case 6: foo = zero; break;
  case 7: foo = zero; break;
  case 8: foo = zero; break;
  case 9: foo = one; break;
  }
  return foo();
}
