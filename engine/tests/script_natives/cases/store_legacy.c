// The legacy game's add-ons were published with it: every one is owned, listed by the player or not. Other store
// apps are still only those the player lists.
#libriary "SteamApiScriptLib"
native int GetDLCenabled(int id);
native int BIsSubscribedApp(int id);

int test_add_ons_with_the_game()
{
	return GetDLCenabled(240480) && GetDLCenabled(452523);
}

int test_other_apps_listed()
{
	return BIsSubscribedApp(3549020) && !BIsSubscribedApp(240480);
}
