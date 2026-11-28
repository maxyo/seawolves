// nullptr comparisons and typeid
object holder;

ref FindNothing()
{
	return nullptr;
}

aref FindNoAttribute(int found)
{
	if (!found) return nullptr;
	aref attribute;
	makearef(attribute, holder);
	return attribute;
}

int TypeName(ref value)
{
	switch (typeid(value))
	{
		case typeid(int): return 1; break;
		case typeid(string): return 2; break;
		case typeid(object): return 3; break;
	}
	return 0;
}

int test_nullptr()
{
	ref r = FindNothing();
	ref h;
	makeref(h, holder);
	return r == nullptr && r is nullptr && h != nullptr;
}

int test_typeid()
{
	int number = 3;
	string text = "a";
	return TypeName(&number) == 1 && TypeName(&text) == 2 && TypeName(&holder) == 3 && typeid(number) == typeid(int);
}

int test_aref_function_returns_nullptr()
{
	aref missing = FindNoAttribute(0);
	aref found = FindNoAttribute(1);
	return missing == nullptr && found != nullptr;
}

int test_nullptr_empties_an_aref()
{
	aref attribute;
	makearef(attribute, holder);
	attribute = nullptr;
	return attribute == nullptr;
}

int fail_nullptr_assigned_to_a_number()
{
	int number = 1;
	number = nullptr;
	return 0;
}

ref unbound;

// expect-runtime-error: invalid ref
int fail_index_an_unbound_global_reference()
{
	int at = 1;
	int value = unbound[at];
	return 0;
}

// expect-runtime-error: invalid ref
int fail_index_an_unbound_local_reference()
{
	ref local;
	int at = 1;
	int value = local[at];
	return 0;
}

ref unboundRows[2];

// expect-runtime-error: assignment through a reference bound to nothing
int fail_assign_to_an_unbound_element_of_a_global_ref_array()
{
	unboundRows[0] = 5;
	return 0;
}

// expect-runtime-error: assignment through a reference bound to nothing
int fail_assign_to_an_unbound_element_of_a_local_ref_array()
{
	ref rows[2];
	rows[1] = "text";
	return 0;
}

// expect-runtime-error: assignment through a reference bound to nothing
int fail_assign_to_an_unbound_element_of_a_new_ref_array()
{
	ref rows = new ref[2];
	rows[0] = 1.5;
	return 0;
}

// expect-runtime-error: call through a reference bound to nothing
int fail_call_through_an_unbound_reference()
{
	ref name;
	call name();
	return 0;
}
