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
