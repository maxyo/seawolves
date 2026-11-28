// SPDX-License-Identifier: GPL-3.0-only
// What the adapters of Caribbean Legend's set read its windows by, beside the common readers (lineages/legacy/lib.c).

// The window's pages: its tabs TABBTN_<n> captioned by TABBTN_<n>_STR, the one shown drawn as the selected tab
// (picture TabSelected). out.{selected (k, -1 none), can (a tab can be pressed), options.o<k>.{label, node}}; the
// number of pages.
int TouchClLib_Pages(aref model, aref out)
{
	out.selected = -1;
	out.can = false;
	aref options;
	makearef(options, out.options);
	for (int n = 1; n <= 8; n++)
	{
		string node = "TABBTN_" + n;
		if (!CheckAttribute(model, node)) continue;
		if (CheckAttribute(model, node + ".images.i0.image") && model.(node).images.i0.image == "TabSelected")
			out.selected = GetAttributesNum(options);
		aref option = TouchLib_Add(options, "o");
		option.label = TouchLib_Trim(TouchLegLib_Label(model, node + "_STR"));
		option.node = node;
		if (UiOffers(node, "press")) out.can = true;
	}
	return GetAttributesNum(options);
}
