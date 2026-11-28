// profile: Caribbean Legend
// a handler that declares parameters receives the event's values in them, also when the event is
// raised inside another handler, or posted for the next frame; a handler without parameters reads them with
// GetEventData. A variable posted by reference ("e") arrives as a reference to it.
native void Event(string name, ...);
native void PostEvent(string name, int delay, string format, ...);
object place;
object counts;

#event_handler("Arrive", "OnArrive");
#event_handler("Arrive", "OnArriveRead");
#event_handler("Tally", "OnTally");
#event_handler("Posted", "OnPosted");
#event_handler("Posted", "OnPostedRead");

void OnArrive(ref where, int visitors, string who, float hour)
{
	counts.arrive = where.id + visitors + who + hour;
	Event("Tally", "l", visitors);
}

void OnArriveRead()
{
	aref where = GetEventData();
	int visitors = GetEventData();
	counts.read = where.id + visitors;
}

void OnTally(int visitors)
{
	counts.tally = visitors * 2;
}

int test_parameters()
{
	place.id = "port";
	Event("Arrive", "alsf", &place, 3, "crew", 1.5);
	return counts.arrive == "port3crew1.5" && counts.read == "port3" && sti(counts.tally) == 6;
}

void OnPosted(ref where)
{
	counts.posted = where.id;
	where.visited = "yes";
}

void OnPostedRead()
{
	ref where = GetEventData();
	counts.posted_read = where.id;
}

void PostPlace(ref where)
{
	PostEvent("Posted", 0, "e", where);
}

int test_post_a_variable()
{
	place.id = "cove";
	PostPlace(&place);
	return 1;
}

int test_posted_variable_arrived()
{
	return counts.posted == "cove" && counts.posted_read == "cove" && place.visited == "yes";
}
