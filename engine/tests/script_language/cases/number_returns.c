// a returned number converts to the function's declared number type, as in C: a float parameter
// keeps the int an argument gave it
float Smaller(float a, float b)
{
	if (a > b) return b;
	return a;
}

int Whole(float f)
{
	return f;
}

int test_int_arguments_of_float_function()
{
	int i = 3;
	float r = Smaller(i, 5);
	return r == 3.0 && Smaller(2.5, 5) == 2.5;
}

int test_float_returned_as_int()
{
	return Whole(2.75) == 2 && Whole(-2.75) == -2;
}
