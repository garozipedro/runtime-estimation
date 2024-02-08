typedef int (*function)(void);

int zero() { return 0; }
int one()  { return 1; }

int main(int argc, char **argv)
{
  function funcs[4];
  funcs[0] = zero;
  funcs[1] = zero;
  funcs[2] = one;
  funcs[3] = zero;

  int acc = 0;
  for (int i = 0; i < 4; ++i)
    acc += funcs[i]();
  return acc;
}
