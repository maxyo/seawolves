// an attribute chain may end a line that the expression continues on the next one
object store;

int test_condition_over_lines()
{
	store.owner = "smith";
	store.stock = "4";
	string owner = "smith";
	if (owner == store.owner
		&& store.stock
		== "4")
		return true;
	return false;
}

int test_choice_over_lines()
{
	store.stock = "4";
	string answer = store.stock
		!= "4" ? "no" : "yes";
	return answer == "yes";
}
