// SPDX-License-Identifier: GPL-3.0-only
// The list of each goods picture MapBest draws, as the game records it while drawing: interface_utils.c
// GoodsPictureDrawn, which MapBest.c calls (a script fix of TEHO 1.7.3; required so that this provider attaches only
// with it). Implements contracts/goods_sections.c.

void TouchAdpGoodsSections_Drawn(aref out)
{
	DeleteAttribute(out, "");
	if (!CheckAttribute(&GameInterface, "GoodsPictures")) return;
	aref drawn;
	makearef(drawn, GameInterface.GoodsPictures);
	CopyAttributes(out, drawn);
}
