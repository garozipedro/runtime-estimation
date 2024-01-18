typedef int (*function)(void);

int zero(void) { return 0; }
int one(void)  { return 1; }

function getfun(int i)
{
  if (i % 2) return one;
  else return zero;
}

void set_again(function *f)
{
  *f = getfun(0);
}

void set_function(function *f1, int i)
{
  if (i < 7) set_again(f1);
  else *f1 = getfun(i);
}

int main()
{
  function foo = 0;
  set_function(&foo, 5);
  return foo();;
}
