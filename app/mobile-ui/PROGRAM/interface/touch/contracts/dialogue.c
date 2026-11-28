// SPDX-License-Identifier: GPL-3.0-only
// The dialogue's adapter (provides "adapter:dialogue"): the game's conversation as the engine hands it over. `ui` is
// the screen's object its events hand over. State(ui, out): out.speaker, out.text (the game's words, its own lines),
// out.replies.r<k>.{link (the game's identity of the reply in the node shown), text, typed (the player types its
// answer), refused (the engine's code of why it refused the answer sent, "" none)}.
extern void TouchAdpDialogue_State(aref ui, aref out);
// The reply `link` chosen; the engine answers it (UiHold).
extern void TouchAdpDialogue_Reply(aref ui, int link);
// In a tap: the phone's text editor over the typed reply's answer, the game's proposal in it; what the player
// accepts goes as the answer, which the engine answers.
extern void TouchAdpDialogue_Type(aref ui, string title);
