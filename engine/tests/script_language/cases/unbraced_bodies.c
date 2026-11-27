// An unbraced body is one statement: an if (with its else), a loop or a switch ends it, and the
// statement after it runs once, after the loop or the if
int FindFirst(int limit, int wanted)
{
	int i;
	for (i = 0; i < limit; i++)
		if (i == wanted) return i;
	return -1;
}

int CountWhile(int limit)
{
	int i = 0;
	int big = 0;
	int small = 0;
	while (i < limit)
	{
		if (i > 2) big++;
		else small++;
		i++;
	}
	return big * 10 + small;
}

int test_for_if_then_return()
{
	return FindFirst(10, 7) == 7 && FindFirst(3, 7) == -1;
}

int test_for_if_then_statement()
{
	int hits = 0;
	int after = 0;
	int i;
	for (i = 0; i < 5; i++)
		if (i > 1) hits++;
	after++;
	return hits == 3 && after == 1 && i == 5;
}

int test_for_if_else_then_statement()
{
	int even = 0;
	int odd = 0;
	int after = 0;
	int i;
	for (i = 0; i < 6; i++)
		if (i % 2 == 0) even++;
		else odd++;
	after++;
	return even == 3 && odd == 3 && after == 1;
}

int test_for_else_if_chain()
{
	int a = 0;
	int b = 0;
	int c = 0;
	int after = 0;
	int i;
	for (i = 0; i < 9; i++)
		if (i < 2) a++;
		else if (i < 5) b++;
		else c++;
	after++;
	return a == 2 && b == 3 && c == 4 && after == 1 && CountWhile(5) == 23;
}

int test_while_if_then_statement()
{
	int i = 0;
	int hits = 0;
	int after = 0;
	while (i < 4)
		if (Next(&i) > 1) hits++;
	after++;
	return hits == 2 && after == 1;
}

int Next(ref counter)
{
	int previous = counter;
	counter = previous + 1;
	return previous;
}

int test_range_for_if_then_statement()
{
	int values[4] = {3, 8, 1, 9};
	int big = 0;
	int after = 0;
	for (int value: values)
		if (value > 5) big++;
	after++;
	return big == 2 && after == 1;
}

int test_if_if_then_statement()
{
	int inner = 0;
	int after = 0;
	int outer = 0;
	if (outer == 1)
		if (inner == 0) inner = 5;
	after++;
	return inner == 0 && after == 1;
}

int Dangling(int a, int b)
{
	int result = 0;
	if (a)
		if (b) result = 1;
		else result = 2;
	else result = 3;
	return result;
}

int test_dangling_else()
{
	return Dangling(1, 1) == 1 && Dangling(1, 0) == 2 && Dangling(0, 1) == 3 && Dangling(0, 0) == 3;
}

int test_loop_as_if_body()
{
	int sum = 0;
	int after = 0;
	int i;
	if (sum == 0)
		for (i = 0; i < 3; i++) sum += i;
	else sum = -1;
	after++;
	if (sum == 0)
		while (sum < 2) sum++;
	after++;
	return sum == 3 && after == 2;
}

int test_switch_as_body()
{
	int result = 0;
	int after = 0;
	int i;
	for (i = 0; i < 3; i++)
		switch (i)
		{
			case 1: result += 10; break;
			case 2: result += 100; break;
		}
	after++;
	return result == 110 && after == 1;
}

int test_break_continue_in_unbraced_if()
{
	int sum = 0;
	int i;
	for (i = 0; i < 10; i++)
	{
		if (i == 2) continue;
		if (i == 5) break;
		sum += i;
	}
	int last = -1;
	for (i = 0; i < 10; i++)
		if (i == 4) break;
		else last = i;
	return sum == 1 + 3 + 4 && last == 3 && i == 4;
}

int SumItems(ref items, int skip)
{
	int sum = 0;
	if (skip == 0) for (int value: items) { sum += value; };
	else sum = -1;
	return sum;
}

int test_loop_in_if_then_separator_then_else()
{
	int items[3] = {1, 2, 4};
	return SumItems(&items, 0) == 7 && SumItems(&items, 1) == -1;
}

int test_nested_loops_unbraced()
{
	int count = 0;
	int after = 0;
	int i;
	int j;
	for (i = 0; i < 3; i++)
		for (j = 0; j < 4; j++)
			if (j > i) count++;
	after++;
	return count == 3 + 2 + 1 && after == 1;
}
