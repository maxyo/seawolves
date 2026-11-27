// A local or a parameter named like a function (names are case-insensitive) is the local wherever the name is not
// called: the functions known while a segment compiles depend on the segments loaded before it
int Exit()
{
	return 7;
}

int Score(int score)
{
	score++;
	return score;
}

int Count(int limit)
{
	int count;
	for (count = 0; count < limit; count++)
	{
	}
	return count;
}

int test_local_read_and_written()
{
	string exit = "";
	exit = "a";
	exit += "b";
	return exit == "ab";
}

int test_parameter()
{
	return Score(2) == 3;
}

int test_loop_variable()
{
	return Count(4) == 4;
}

int test_call_beside_the_local()
{
	int exit = 1;
	return Exit() + exit == 8;
}
