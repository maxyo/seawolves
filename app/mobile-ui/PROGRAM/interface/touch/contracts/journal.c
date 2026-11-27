// SPDX-License-Identifier: GPL-3.0-only
// The journal's adapter (provides "adapter:journal"): the hero's records in parts (quests, documents, assets,
// statistics), each of the game's pages; a page lists entries, the text of the one open, or the game's tables. `ui` is
// the screen's object its events hand over.
//
// State(ui, out), once a build:
//   title, date ("" none); sections (the hero sections open from here); part: the part of the page shown
//   pages.p<k>.{id, part ("quests", "docs", "assets", "stats"), label, selected, count (absent: not known)}
//   entries (the page lists entries).e<k>.{label, open}; text (absent: none open).{title, records.r<k>.{date ("" none),
//     text}}
//   tables.t<k>.{id, group ("" the page's own; "battles", "by_nation", "other"), choose (a row of it can be chosen),
//     columns.c<n>.{label}, rows.r<k>.{selected, cells.c<n>.{text, image (absent: none).{kind, group, image, uv}}}}
// Opening an entry may take the game several frames (its text closed, another page looked at): the adapter awaits
// them, and the screen's view of before stays meanwhile.
extern void TouchAdpJournal_State(aref ui, aref out);
extern void TouchAdpJournal_Page(aref ui, string page);             // pages p<k>.id
extern void TouchAdpJournal_Open(aref ui, int entry);               // entries e<k>
extern void TouchAdpJournal_Find(aref ui, string quest);            // in a build: the entry of the game's quest id
extern void TouchAdpJournal_Choose(aref ui, string table, int row); // tables t<k>.id, its rows r<k>
extern void TouchAdpJournal_Section(aref ui, string section);       // another hero section (hero, ship, …)
extern void TouchAdpJournal_Leave(aref ui);
