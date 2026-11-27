// profile: Caribbean Legend
// fts(value, n) writes n digits after the point (the scripts cut the text at its '.'); strcut refuses
// a range that starts before the text
int test_fts_digits_after_point()
{
	float share = 0.25;
	return fts(20.5, 2) == "20.50" && fts(20.0, 2) == "20.00" && fts(3, 1) == "3.0" && fts(share, 0) == "0"
		&& fts(0.25) == "0.25";
}

int TextAfterPoint(string text)
{
	int point = FindSubStr(text, ".", 0);
	return strcut(text, point, point + 1) == ".5";
}

int test_point_found()
{
	return TextAfterPoint(fts(7.5, 2));
}

// the error stops nothing: the result is empty
int fail_strcut_before_text()
{
	string cut = strcut("a text longer than a short string", -1, 0);
	return cut != "";
}
