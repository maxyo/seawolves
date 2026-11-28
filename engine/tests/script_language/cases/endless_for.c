// A for loop whose body resets its counter never ends: stopped at the call's instruction limit (endless_loop.c).
int fail_endless_for()
{
	int sum = 0;
	for (int i = 0; i < 10; i++)
	{
		i = 0;
	}
	return 1;
}
