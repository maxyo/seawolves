// makearef takes .(expression) in its chain as every other attribute chain does: the attribute named by the text,
// created when missing
object tree;

int test_named_by_an_attribute()
{
	tree.names.first = "gold";
	tree.gold = "3";
	aref names;
	makearef(names, tree.names);
	aref found;
	makearef(found, tree.(names.first));
	return sti(found) == 3;
}

int test_named_by_text_created()
{
	string side = "left";
	aref made;
	makearef(made, tree.hands.(side + "_hand"));
	return CheckAttribute(&tree, "hands.left_hand");
}
