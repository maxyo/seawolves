// profile: Caribbean Legend
// a handler that declares parameters receives the event's values in them, also when the event is
// raised inside another handler; a handler without parameters reads them with GetEventData
native void Event(string name, ...);
object place;
object counts;

#event_handler("Arrive", "OnArrive");
#event_handler("Arrive", "OnArriveRead");
#event_handler("Tally", "OnTally");

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
