// profile: Caribbean Legend
// an array of references assigned to another one: each element refers to what the other's refers to, the size follows
// (Caribbean Legend moves its weather to the next day with `WeatherState = WeatherStateNext`)
ref today[];
ref tomorrow[];

int test_global_arrays()
{
	@tomorrow = 2;
	tomorrow[0] = new int[1];
	tomorrow[1] = new int[1];
	tomorrow[1][0] = 5;
	@today = 1;
	today = tomorrow;
	today[0][0] = 7;
	return @today == 2 && today[1][0] == 5 && tomorrow[0][0] == 7;
}

int test_local_arrays()
{
	ref first[2];
	ref second[2];
	first[0] = new string[1];
	first[1] = new string[1];
	first[1][0] = "b";
	second = first;
	return second[1][0] == "b";
}
