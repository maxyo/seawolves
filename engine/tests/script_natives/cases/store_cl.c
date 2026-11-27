// profile: Caribbean Legend
// The store's answers come from the player's settings (config/seawolves/settings.json of this check).
#libriary "SteamApiScriptLib"

native bool GetDLCenabled(int id);
native int BIsSubscribedApp(int id);
native string GetPersonaName();
native bool IsVerified();
native bool Verify();

int test_owned_add_on()
{
	return GetDLCenabled(2230982) && !GetDLCenabled(2230983);
}

int test_owned_app()
{
	return BIsSubscribedApp(3549020) && !BIsSubscribedApp(3549021);
}

int test_persona_name()
{
	return GetPersonaName() == "Капитан Тест";
}

int test_purchase_check_inert()
{
	Verify();
	return IsVerified();
}
