typedef int (*function)(void);

int zero() { return 0; }
int one()  { return 1; }

void set_function(function *f1)
{
  *f1 = one;
}

int main()
{
  function foo = 0;
  set_function(&foo);
  return foo();;
}
