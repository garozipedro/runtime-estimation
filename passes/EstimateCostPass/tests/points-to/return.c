int one() { return 1; }

typedef int (*func)(void);

func get_func() { return one; };

int main()
{
  return get_func()();
}
