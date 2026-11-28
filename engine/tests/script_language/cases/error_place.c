// a runtime error names the statement that was executing; an error while the engine dispatches a posted event,
// before its handler runs, names the event, not the last line of the script that ran before it
native void PostEvent(string name, int delay, string format, ...);
#event_handler("TwoValues", "OnTwoValues");

void OnTwoValues(int first, int second)
{
}

// expect-runtime-error: RUNTIME ERROR in error_place.c(16): invalid index 7
int fail_index_names_its_statement()
{
	int values[2];
	int at = 7;
	OnTwoValues(1, 2);
	int value = values[at];
	return 0;
}

// expect-runtime-error: RUNTIME ERROR in event TwoValues: function 'OnTwoValues' called with 1 arguments
int fail_posted_event_without_its_values()
{
	PostEvent("TwoValues", 0, "l", 1);
	return 0;
}
