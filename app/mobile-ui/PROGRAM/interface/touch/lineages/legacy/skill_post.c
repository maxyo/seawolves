// SPDX-License-Identifier: GPL-3.0-only
// The post that lends a skill as the game names it: RPGUtilite.c GetSkillOfficerPost, which
// GetSummonSkillFromNameSimple asks too (a script fix of TEHO 1.7.3). Implements contracts/skill_post.c.

string TouchAdpSkillPost_Of(string skill)
{
	return GetSkillOfficerPost(skill);
}
