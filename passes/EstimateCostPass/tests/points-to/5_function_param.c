typedef int (*function)(void);

int zero() { return 0; }
int one()  { return 1; }

void swap_function(function *f1, function *f2)
{
  function faux = f1;
  *f1 = *f2;
  *f2 = faux;
}

void dont_set_function(function *f1)
{
}

void set_function(function *f1)
{
  *f1 = one;
}

int main()
{
  function foo = 0;
  set_function(&foo);
  dont_set_function(&foo);
  set_function(&foo);
  dont_set_function(&foo);
  return foo();;
}
