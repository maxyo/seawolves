// SPDX-License-Identifier: GPL-3.0-only
// The flag change's refusal as the game names it: interface_utils.c FlagChangeRefusal, which NationRelation.c
// FlagsProcess asks too (a script fix of TEHO 1.7.3). Implements contracts/flag_change.c.

string TouchAdpFlagChange_Refusal()
{
	return FlagChangeRefusal();
}
