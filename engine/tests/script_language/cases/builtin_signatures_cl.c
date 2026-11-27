// profile: Caribbean Legend
// Built-ins that Caribbean Legend declares with its own engine's types and argument counts.
native float GetDeltaTime();
native int findSubStr(ref text, string part, int start);
native int SaveEngineState(string file);

int test_return_types()
{
	float delta = GetDeltaTime();
	string text = "abcabc";
	return delta >= 0.0 && findSubStr(&text, "ca", 0) == 2 && findSubStr(&text, "x", 0) == -1;
}

int test_argument_ranges()
{
	return fts(1.5) == "1.5" && fts(2.25, 2) == "2.25";
}
