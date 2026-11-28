// SPDX-License-Identifier: GPL-3.0-only
// The goods pictures of a map's colony window as the game drew them (provides "adapter:goods_sections"): out.p<n>.
// {good, x, y, section} in drawing order, section the game's name of the list the picture belongs to. Optional to the
// maps: in a game that does not record them the map adapter reads the lists by the pictures' places.
extern void TouchAdpGoodsSections_Drawn(aref out);
