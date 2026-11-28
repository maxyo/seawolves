// SPDX-License-Identifier: GPL-3.0-only
// The maps' adapter (provides "adapter:map"): a map of the game to look at, the game's markers on it, the maps the
// hero carries to choose from, and the colony the player pointed at. Where a point is and which colony it finds is
// the game's: the adapter hands it the point and shows what it answers. `ui` is the screen's object its events hand
// over. An image is {kind, group, image, uv}.
//
// State(ui, out), once a build:
//   title ("" none); map.{image, base, view ("x,y" and "x0,y0,x1,y1" in the map's own units)}
//   markers.m<k>.{image, id, x, y, w, h} (centre and size in the map's units)
//   maps.m<k>.{key (the game's map), label, selected, can} (absent: one map); mapsTitle
//   colony (absent: none pointed at).{title, nation (our flag icon), sub, factsTitle, facts.f<k>.{k, v},
//     notes.n<k>.{text}, sections.s<k>.{title, page ("trade", "assistant"), goods.g<k>.{image, label, value (absent:
//     none)}, lines.l<k>.{text}}}
extern void TouchAdpMap_State(aref ui, aref out);
extern void TouchAdpMap_Point(aref ui, string at); // "x,y" in the map's units: the game shows what is there
extern void TouchAdpMap_Release(aref ui);          // the colony shown let go
extern void TouchAdpMap_Choose(aref ui, int map);  // maps m<k>
extern void TouchAdpMap_Leave(aref ui);
