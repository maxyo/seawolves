// An attribute that holds a script reference (Caribbean Legend's typed attributes, DynamicObject_SetActorNative)
// reads as a reference to the variable it names.
#libriary "script_check_test_library"

native void ScriptCheckSetReference(ref holder, ref target);
native void AppendAttributes(ref destination, ref source);

object attrref_characters[3];
object attrref_session;

bool attrref_is_bartender(ref chr)
{
	return chr.id == "Bartender";
}

int test_attribute_reference_names_its_target()
{
	attrref_characters[2].id = "Bartender";
	ScriptCheckSetReference(&attrref_session, &attrref_characters[2]);
	if (attrref_session.target is nullptr) return false;
	ref actor = attrref_session.target;
	if (actor.id != "Bartender") return false;
	actor.seated = "1";
	if (attrref_characters[2].seated != "1") return false;
	if (!attrref_is_bartender(attrref_session.target)) return false;
	object copy;
	CopyAttributes(&copy, &attrref_session);
	ref copied = copy.target;
	return copied.id == "Bartender";
}

int test_text_makes_the_attribute_plain()
{
	attrref_characters[1].id = "Guard";
	ScriptCheckSetReference(&attrref_session, &attrref_characters[1]);
	attrref_session.target = "plain";
	return attrref_session.target == "plain";
}

int test_append_keeps_the_reference()
{
	attrref_characters[0].id = "Smith";
	ScriptCheckSetReference(&attrref_session, &attrref_characters[0]);
	object appended;
	AppendAttributes(&appended, &attrref_session);
	ref actor = appended.target;
	return actor.id == "Smith";
}
