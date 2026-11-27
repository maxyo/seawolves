// expect-error: unclosed_initializer.c(6): No matching ')' in expression
// a bracket left open in a declaration's initializer is reported on its line, not past the function's end
int test_never_runs()
{
	int k = 1;
	int r = (k == 1;
	return r;
}
