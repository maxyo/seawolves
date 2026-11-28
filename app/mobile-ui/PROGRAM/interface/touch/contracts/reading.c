// SPDX-License-Identifier: GPL-3.0-only
// The reading windows' adapter (provides "adapter:reading"): a short window of the game over the running game that
// the player reads and answers, if at all, with one of at most two answers: a signal, a question, a notice, an
// interlude, a page of text (a lesson, a message). `ui` is the screen's object its events hand over.
//
// State(ui, out), once a build:
//   title (the game's caption, "" none); lines.l<k> (the game's texts in its order, "" left out); amount (the sum the
//   window is about, as written, "" none); picture (absent: none).{kind, group, image, uv}: the window's own picture
//   flags.f<k>.{icon} (the nations it is about, our flag icons); relation ("war", "alliance", "peace": between the two
//   flags, "" none)
//   accept, decline (absent: the window has no such answer).{label, can, reason}: the game's answers
//   check (absent: none).{label, on, can, reason}: a switch the window offers beside its answers (show such lessons)
extern void TouchAdpReading_State(aref ui, aref out);
extern void TouchAdpReading_Accept(aref ui);
extern void TouchAdpReading_Decline(aref ui);
extern void TouchAdpReading_Check(aref ui, bool on);
extern void TouchAdpReading_Leave(aref ui); // the game's own way out of a window without answers (its back)
