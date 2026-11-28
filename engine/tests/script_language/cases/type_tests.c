// x is type, x !is type: nullptr, value types, arrays by element type, function references held by attributes
object registry;

int Triple(int value)
{
	return value * 3;
}

ref NothingFound()
{
	return nullptr;
}

bool HoldsArefs(ref list)
{
	return list is aref[];
}

bool HoldsRefs(ref list)
{
	return !(list !is ref[]);
}

int test_not_null()
{
	ref missing = NothingFound();
	ref held;
	makeref(held, registry);
	bool both = held !is nullptr && missing is nullptr;
	return both && !(missing !is nullptr) && (held !is nullptr);
}

int test_value_types()
{
	int count = 2;
	string label = "x";
	float share = 0.5;
	fref hook = &Triple;
	return count is int && label is string && share is float && hook is fref && registry is object &&
		count !is string && label !is int && share !is fref && count !is int[];
}

int test_attribute_function()
{
	DeleteAttribute(&registry, "");
	registry.hook := &Triple;
	registry.label = "plain words";
	registry.number = 5;
	bool named = registry.hook is fref && registry.label !is fref && registry.number !is fref;
	return named && registry.hook(2) == 6;
}

int test_missing_attribute_is_no_function()
{
	DeleteAttribute(&registry, "");
	return registry.nothing !is fref && !(registry.deeper.nothing is fref);
}

int test_arrays()
{
	aref parts[];
	ref people[2];
	int numbers[3];
	return HoldsArefs(&parts) && !HoldsArefs(&people) && !HoldsArefs(&numbers) && HoldsRefs(&people) &&
		!HoldsRefs(&parts) && numbers is int[] && numbers !is string[];
}

int test_ref_default()
{
	DeleteAttribute(&registry, "");
	registry.group.first = "1";
	registry.group.second = "2";
	ref group = registry.group$ref(nullptr);
	ref none = registry.absent$ref(nullptr);
	return group !is nullptr && @group == 2 && none is nullptr;
}

bool FindGroup(ref found)
{
	found = registry.group$aref;
	return true;
}

int test_ref_argument_written_through()
{
	DeleteAttribute(&registry, "");
	registry.group.first = "1";
	aref result;
	FindGroup(&result);
	return @result == 1 && GetAttributeName(result) == "group";
}

int test_resize_to_empty()
{
	aref found[];
	@found = 2;
	@found = 0;
	int emptied = @found;
	@found = 1;
	return emptied == 0 && @found == 1;
}
