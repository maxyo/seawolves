// SPDX-License-Identifier: GPL-3.0-only
// Authored presentation adapter for the nations screen: which flags the ship may raise, by the
// rule of NationRelation.c SetNewNation (a Flag* perk unlocks the choice; then the hero's own
// nation, the nations whose Flag perk he has and his patent's nation). No game rules change.
#event_handler("AndroidNationFlags", "AndroidNationFlags");
void AndroidNationFlags()
{
    ref mchar = GetMainCharacter();
    DeleteAttribute(&GameInterface, "AndroidFlags");
    int unlocked = false;
    if (IsCharacterPerkOn(mchar, "FlagPir")) unlocked = true;
    if (IsCharacterPerkOn(mchar, "FlagEng")) unlocked = true;
    if (IsCharacterPerkOn(mchar, "FlagFra")) unlocked = true;
    if (IsCharacterPerkOn(mchar, "FlagSpa")) unlocked = true;
    if (IsCharacterPerkOn(mchar, "FlagHol")) unlocked = true;
    int n;
    int allowed;
    string name;
    for (n = 0; n < 5; n++)
    {
        name = GetNationNameByType(n);
        allowed = false;
        if (unlocked)
        {
            if (IsCharacterPerkOn(mchar, "Flag" + NationShortName(n))) allowed = true;
            if (sti(mchar.nation) == n) allowed = true;
            if (isMainCharacterPatented())
            {
                if (sti(Items[sti(mchar.EquipedPatentId)].Nation) == n) allowed = true;
            }
        }
        GameInterface.AndroidFlags.(name) = allowed;
    }
}
