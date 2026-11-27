// The localisation natives, through the VM: what they read from the running scripts (script_check answers each text
// with the value it asked for).
// profile: Caribbean Legend
native string DLGO(string text, ...);
native string DLG_Convert(string key, string file, ...);
native string DLG_ConvertE(string key, string file, ...);
native void DLG_ReplaceAllMatches(ref text, string key, string replacement, ...);
native string StringFromKey(string key, ...);

#define TEXTS_LIMIT 12
#define TEXTS_RATIO 0.5

string textsTitle = "Deck";
object textsBox;
object hero;
object partner;
ref pchar;
ref CharacterRef;

string Describe(int count, ref who)
{
	return "" + (count * 2) + who.name;
}

int test_player_and_partner()
{
	pchar = &hero;
	CharacterRef = &partner;
	hero.name = "Charles";
	partner.ship.name = "Fortune";
	return DLGO("player:name") == "Charles" && DLGO("npc:ship.name") == "Fortune" && DLGO("npc:name") == "<none>";
}

int test_context()
{
	object item;
	item.price.base = "40";
	aref price;
	makearef(price, item.price);
	return DLGO("context:price.base", &item) == "40" && DLGO("context:base", price) == "40" &&
	       DLGO("context:price.base") == "<none>";
}

int test_script_call()
{
	object item;
	item.name = "blade";
	// the text argument becomes the int parameter, the reference is the context
	return DLGO("call:Describe:21", &item) == "42blade" && DLGO("call:NoSuchFunction:1", &item) == "<none>";
}

string Twice(ref value)
{
	string once = value;
	return once + once;
}

string Pad(string word, string edge = "-")
{
	return edge + word + edge;
}

int test_defines_and_globals()
{
	textsBox.size.w = "3";
	return DLGO("define:TEXTS_LIMIT") == "12" && DLGO("define:TEXTS_RATIO") == "0.5" && DLGO("define:NONE") == "<none>" &&
	       DLGO("define:textsTitle") == "Deck" && DLGO("define:textsBox.size.w") == "3" &&
	       DLGO("define:textsBox.none") == "<none>";
}

int test_ref_parameters_and_defaults()
{
	return DLGO("call1:Twice:ab") == "abab" && DLGO("call1:Pad:x") == "-x-";
}

int test_convert()
{
	object item;
	item.weight = "3";
	return DLG_Convert("context:weight", "items.txt", &item) == "3" && DLG_Convert("key", "missing") == "key" &&
	       DLG_ConvertE("key", "missing") == "";
}

int test_string_from_key()
{
	return StringFromKey("greeting", "a", 2) == "greeting|a|2" && StringFromKey("plain") == "plain";
}

int test_replace_all_matches()
{
	string text = "a<br>b<br>c";
	DLG_ReplaceAllMatches(&text, "<br>", "<br><br>");
	string tail = "x-x-x";
	DLG_ReplaceAllMatches(&tail, "x", "y", 1);
	return text == "a<br><br>b<br><br>c" && tail == "x-y-y";
}
