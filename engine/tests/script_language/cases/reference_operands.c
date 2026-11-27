// A reference on the left of an operator is its value: the operation gives a new value and leaves the referenced
// variable as it was; an assignment through the reference still writes it
int Less(ref value)
{
	return value - 1;
}

float Scaled(ref value, float by)
{
	return value * by;
}

int Negated(ref value)
{
	return -value;
}

int test_return_of_an_operation()
{
	int x = 5;
	return Less(&x) == 4 && x == 5;
}

int test_reference_kept()
{
	int x = 4;
	return Scaled(&x, 0.5) == 2.0 && x == 4;
}

int test_unary()
{
	int x = 3;
	return Negated(&x) == -3 && x == 3;
}

void Next(ref value)
{
	int next = value + 1;
	value = next;
}

int test_assignment_writes_through()
{
	int x = 1;
	Next(&x);
	return x == 2;
}

int test_compound_writes_through()
{
	int x = 1;
	ref r;
	makeref(r, x);
	r += 2;
	return x == 3;
}
