typedef int (*function)(void);

int zero() { return 0; }
int one()  { return 1; }

struct S {
  function a, b;
};

int main(int argc, char **argv)
{
//  struct S s = { .a = zero, .b = one };
  struct S s;
  s.a = zero; s.b = one;

  if (argc > 1) s.a = s.b;
  return s.a() + s.b();
}
