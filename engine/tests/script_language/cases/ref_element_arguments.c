// profile: Caribbean Legend
// &a[i] where a is a reference to an array (a local ref to a new array): the element, to a script function's
// and to a native's ref parameter
native int ScriptCheckAddOne(ref value);
#libriary "script_check_test_library"

int ByRef(ref value)
{
	return value + 1;
}

int test_script_function()
{
	ref cells = new int[2];
	cells[1] = 4;
	return ByRef(&cells[1]) == 5;
}

int test_native()
{
	ref cells = new int[2];
	cells[1] = 4;
	return ScriptCheckAddOne(&cells[1]) == 5;
}

// expect-runtime-error: &[5]: not an element of an array
int fail_element_beyond_the_array()
{
	ref cells = new int[2];
	return ByRef(&cells[5]);
}
