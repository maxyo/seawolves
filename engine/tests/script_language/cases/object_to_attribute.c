// An object assigned to an attribute (a literal, a variable or a reference to one) replaces the
// attribute's value and children with a copy of its tree; an aref still gives its value
object shelf[2];
object store;

int test_literal()
{
	object o;
	o.slot = {count = 1, label = "x"};
	string name = "other";
	o.(name) = {count = 2};
	return sti(o.slot.count) == 1 && o.slot.label == "x" && sti(o.other.count) == 2;
}

int test_variable_is_copied()
{
	object source;
	source.k = 5;
	source.k.deep = 6;
	store.copy = source;
	source.k = 9;
	return sti(store.copy.k) == 5 && sti(store.copy.k.deep) == 6;
}

int test_reference_to_object()
{
	object source;
	source.k = 3;
	ref r = &source;
	object o;
	o.copy = r;
	return sti(o.copy.k) == 3;
}

int test_replaces_children()
{
	object o;
	o.box.old = 1;
	o.box = {fresh = 2};
	return !CheckAttribute(&o, "box.old") && sti(o.box.fresh) == 2;
}

int test_array_element_destination()
{
	shelf[1].item = {weight = 4};
	return sti(shelf[1].item.weight) == 4 && !CheckAttribute(&shelf[0], "item");
}

int test_aref_gives_value()
{
	object source;
	source.k = "val";
	source.k.deep = 6;
	aref a;
	makearef(a, source.k);
	object o;
	o.copy = a;
	return o.copy == "val" && !CheckAttribute(&o, "copy.deep");
}

void SetNamed(ref target, string name, ref value)
{
	target.(name) = value;
}

int test_scalar_through_reference_argument()
{
	object o;
	SetNamed(&o, "flag", true);
	SetNamed(&o, "n", 7);
	return sti(o.flag) == 1 && sti(o.n) == 7;
}
