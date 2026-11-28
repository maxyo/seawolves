// An endless script loop stops with an error and the script stack (the call's instruction limit) instead of
// hanging the engine. No script runs after it, so each endless case is the only failing function of its file.
int Spin(int n)
{
	while (true)
	{
		n++;
	}
	return n;
}

int fail_endless_loop()
{
	Spin(0);
	return 1;
}
