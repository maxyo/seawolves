// && and || stop at the first operand that decides, as in TEHO's compiler: the games read an attribute after
// CheckAttribute in the same condition
object tree;
int calls;

int Counted(int value)
{
	calls++;
	return value;
}

int test_and_stops_at_false()
{
	calls = 0;
	return !(Counted(0) && Counted(1)) && calls == 1;
}

int test_or_stops_at_true()
{
	calls = 0;
	return (Counted(1) || Counted(0)) && calls == 1;
}

int test_absent_attribute_not_read()
{
	DeleteAttribute(&tree, "");
	return !(CheckAttribute(&tree, "count") && sti(tree.count) > 0);
}
