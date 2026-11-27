// Built-ins of the Caribbean Legend dialect, through the VM.
native void ArraySort(ref values, ...);
native ref SmartSort(ref records, string field, string type, bool ascending, int offset, string prefix, ...);
native string Sprintf(string format, ...);
native int attrrename(aref attribute, string name);
native void AppendAttributes(ref destination, ref source);
native float ceilf(float value);
native string GetCallerName();

int test_array_sort()
{
	int values[4];
	values[0] = 3; values[1] = -1; values[2] = 7; values[3] = 0;
	ArraySort(&values);
	return values[0] == -1 && values[1] == 0 && values[2] == 3 && values[3] == 7;
}

int test_smart_sort()
{
	object list;
	list.r1.price = "30";
	list.r2.price = "5";
	list.r3.price = "12";
	SmartSort(&list, "price", "int", true, 0, "r");
	aref first = GetAttributeN(&list, 0);
	aref last = GetAttributeN(&list, 2);
	return GetAttributeName(first) == "r2" && GetAttributeName(last) == "r1";
}

int test_sprintf()
{
	return Sprintf("%d/%s/%.1f", 4, "x", 2.25) == "4/x/2.2";
}

int test_attributes()
{
	object a;
	object b;
	a.keep = 1;
	b.add = 2;
	AppendAttributes(&a, &b);
	aref k = GetAttributeN(&a, 0);
	return attrrename(k, "kept") && CheckAttribute(&a, "kept") && CheckAttribute(&a, "add") && !CheckAttribute(&a, "keep");
}

string Inner()
{
	return GetCallerName();
}

string Outer()
{
	return Inner();
}

int test_caller_and_math()
{
	return Outer() == "Outer" && ceilf(1.2) == 2.0;
}
