// SPDX-License-Identifier: GPL-3.0-only
// The alchemy adapter (provides "adapter:alchemy"): the recipes the hero knows, and as many batches of one as the
// player sets, made of the hero's items. How many batches the items make, how long they take and the making itself
// are the game's. `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   title ("" none); recipes.r<k>.{key, label, image, kind (a string key of the item's kind, "" none), selected, can}
//   recipe.{key, label, image, yield (what a batch makes), text (its description)} (absent: none shown)
//   parts.p<k>.{key, label, image, have (the hero's and the pot's), per (a batch takes), catalyst (not used up by a
//     batch)}
//   batches: how many batches the game can make of them
extern void TouchAdpAlchemy_State(aref ui, aref out);
extern void TouchAdpAlchemy_Choose(aref ui, int recipe); // recipes r<k>
extern int TouchAdpAlchemy_Minutes(aref ui, int batches); // how long the game takes to make that many
// Makes that many batches of the shown recipe; false when the game makes none.
extern bool TouchAdpAlchemy_Make(aref ui, int batches);
extern void TouchAdpAlchemy_Leave(aref ui);
