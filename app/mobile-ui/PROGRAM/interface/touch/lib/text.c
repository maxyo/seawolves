// SPDX-License-Identifier: GPL-3.0-only
// Text helpers of the touch interface's screens: figures of the game's captions and numbers as the screens write
// them. Strings are bytes here; what these look for is ASCII.

// A line break, and the bytes that end a line: the language's strings have no escapes.
string TouchLib_Newline()
{
	return "
";
}

string TouchLib_LineEnds()
{
	return "

 ";
}

// The bytes [from, to) of `text`, "" for an empty range (strcut takes none).
string TouchLib_Cut(string text, int from, int to)
{
	if (to <= from) return "";
	return strcut(text, from, to - 1);
}

bool TouchLib_StartsWith(string text, string start)
{
	int size = strlen(start);
	if (size > strlen(text)) return false;
	return TouchLib_Cut(text, 0, size) == start;
}

bool TouchLib_Digit(string text, int at)
{
	return findSubStr("0123456789", strcut(text, at, at), 0) >= 0;
}

// The first position at or after `from` of a byte that is (`digit`) or is not a digit, -1 for none.
int TouchLib_FindDigit(string text, int from, bool digit)
{
	for (int i = from; i < strlen(text); i++)
	{
		if (TouchLib_Digit(text, i) == digit) return i;
	}
	return -1;
}

// 1500 -> "1 500".
string TouchLib_Thousands(int value)
{
	string digits = "" + abs(value);
	string out = "";
	int size = strlen(digits);
	for (int i = 0; i < size; i++)
	{
		if (i > 0 && (size - i) % 3 == 0) out += " ";
		out += strcut(digits, i, i);
	}
	if (value < 0) return "-" + out;
	return out;
}

// A caption with a figure, "Трюм:\n189 / 199" or "Дедвейт: 189 / 700": out.label (without its colon) and out.value
// (on one line); false for a text without a separated value ("Магазин").
bool TouchLib_Figure(string text, aref out)
{
	int line = findSubStr(text, TouchLib_Newline(), 0);
	if (line < 0)
	{
		line = findSubStr(text, ":", 0);
		if (line < 0 || TouchLib_FindDigit(text, line, true) < 0) return false;
	}
	int end = line;
	while (end > 0 && findSubStr(": ", strcut(text, end - 1, end - 1), 0) >= 0) end--;
	out.label = TouchLib_Cut(text, 0, end);
	int value = line + 1;
	while (value < strlen(text) && strcut(text, value, value) == " ") value++;
	out.value = UiOneLine(TouchLib_Cut(text, value, strlen(text)));
	return true;
}

// The text without the spaces and line ends around it.
string TouchLib_Trim(string text)
{
	int begin = 0;
	int end = strlen(text);
	while (begin < end && findSubStr(TouchLib_LineEnds(), strcut(text, begin, begin), 0) >= 0) begin++;
	while (end > begin && findSubStr(TouchLib_LineEnds(), strcut(text, end - 1, end - 1), 0) >= 0) end--;
	return TouchLib_Cut(text, begin, end);
}

// What a caption says after its colon, "Мораль: Хорошая" -> "Хорошая"; the whole text without one.
string TouchLib_AfterColon(string text)
{
	int colon = findSubStr(text, ":", 0);
	if (colon < 0) return TouchLib_Trim(text);
	return TouchLib_Trim(TouchLib_Cut(text, colon + 1, strlen(text)));
}

bool TouchLib_EndsWith(string text, string end)
{
	int size = strlen(end);
	if (size > strlen(text)) return false;
	return TouchLib_Cut(text, strlen(text) - size, strlen(text)) == end;
}

// A string of two places, {1} and {2}: UiFill fills the first.
string TouchLib_FillTwo(string text, string first, string second)
{
	int at = findSubStr(text, "{2}", 0);
	if (at >= 0) text = TouchLib_Cut(text, 0, at) + second + TouchLib_Cut(text, at + 3, strlen(text));
	return UiFill(text, first);
}
