// profile: Caribbean Legend
// Caribbean Legend's controls, script-test and speed-colour built-ins, declared as its scripts declare them.
native void SetControlHold(int control, int group, bool hold);
native void SetHoldThreshold(int milliseconds);
native void ResetHoldControls();
native int SetHoldControls(int primary, int hold);
native void SetClassicCombatControls(bool classic);
native void SetControlScheme(int control, int scheme);
native void UpdateControlGroup(int group);
native void RunScriptTests();
native void ReInitSpeedColors(ref colors);

object colors;

int test_controls()
{
	int control = CreateControl("ScriptCheckAttack");
	MapControl(control, 1, 8);
	SetControlScheme(control, 2);
	SetControlHold(control, 8, true);
	SetHoldThreshold(200);
	SetClassicCombatControls(false);
	UpdateControlGroup(8);
	ResetHoldControls();
	SetHoldControls(-1, -1);
	RunScriptTests();
	return true;
}

int test_speed_colors()
{
	colors.a1.speed = 0.0;
	colors.a1.color = argb(255, 255, 0, 0);
	colors.a2.speed = 1.0;
	colors.a2.color = argb(255, 0, 255, 0);
	ReInitSpeedColors(&colors);
	return true;
}

// The error is reported and the script goes on.
int fail_speed_colors_of_a_number()
{
	int number = 1;
	ReInitSpeedColors(&number);
	return false;
}
