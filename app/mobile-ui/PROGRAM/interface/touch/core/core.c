// SPDX-License-Identifier: GPL-3.0-only
// The shared core of the touch interface's scripts, attached before every other one of them: the natives of the
// screens and the model the engine gives a screen's build.
#libriary "UiScripts"
// The capability ids TOUCH_CAP_<ID>: a define is the program's from its first segment on, so the core includes them
// once for every later one.
#include "interface/touch/capabilities.h"

native void UiShow(aref view);
native void UiPredict(aref view);
native void UiOn(string id, string verb);
native void UiModal(string id, string cancel);
// In a build: the back key raises the tap `id` while this view is shown (a sheet of the screen's own open).
native void UiBackTo(string id);
native int UiPending(string id);
native string UiText(string key);

// Widget actions: UiSendTo(&action) takes {node, verb, value, row (absent: the node's own widget), guard (the row
// identity the action must still find), lead (sent right before the value)}.
native void UiSend(string node, string verb, string value);
native void UiSendTo(aref action);
native void UiBack(string node);
native int UiOffers(string node, string verb);

// Waiting for the game: after UiHold the view shown stays until the game has answered what was sent (at most
// `seconds`); a build under it reads UiAnswer() ("", "waiting", "applied", "refused", "late") and says UiAwait() while
// its game's answer takes more frames.
native void UiHold(float seconds);
native string UiAnswer();
native void UiAwait();
native void UiRebuild();
native int UiFirst();
// In a build: the subject another part of the interface asked this screen to show, out.{character, position, tab,
// quest} (-1 and "" where it names none), taken once; 0 without one.
native int UiSubject(string screen, aref out);

// The menus' engine commands: a host action of the application ("app-about", "app-exit-to-main-menu", …), the
// game's quick save or load after the menu resumes ("save", "load"), a title command ("New", "Load", "Options",
// "Credits", "Exit", "Continue"); what the title opens once (UiAskTitle: "", "Load", "Options"; UiTitleAsked takes
// it, also the host's "Continue"); where the menus run from ("title", "game").
native void UiHost(string action);
native void UiQuickGame(string which);
native void UiTitle(string command);
native void UiAskTitle(string intent);
native string UiTitleAsked();
native void UiMenusFrom(string where);
native void UiToast(string text);
native void UiRead(string title, string text);
native void UiEditText(string node, string title);
// The screen cannot do what it is for: the build or tap fails with the text and the interface closes.
native void UiError(string text);

// The figures of the game's texts, read as every screen reads them; UiAmount and UiNumber fail naming `what` when the
// text holds none.
native int UiAmount(string text, string what);
native int UiHasAmount(string text);
native float UiNumber(string text, string what);
native string UiDecimal(string text);
native string UiOneLine(string text);
native string UiFill(string text, string argument);

// The open deal of a quantity control, the amount the game answers until the player presses its button: spec {key,
// element, origin, other, low, high, answer (our side as the game holds it now), cutReason, more, less (string keys),
// disabled, reason, commit, offer (the event each move raises with the change asked)}; out {value, change, button
// (absent while the amount is unchanged)}.
native void UiDeal(aref spec, aref out);

// A whole number the game's scripts hold, by its global's name ("iHourWait") or an attribute of a global object or
// reference ("refStore.Goods.Sugar.TradeType"): reaches the globals of the game's open interface, which a segment
// attached before it cannot name. Fails when absent or not a whole number.
native int UiGameInt(string path);

// The entries of `list` in the order of their whole number `field`, stable.
native void UiOrder(aref list, string field, int descending);

// The layout of a tree of icons (the perk tree): nodes n<k>.id in their order, edges e<k>.{from, to}; out {valid,
// width, height, places.p<k>.{left, top}, links.l<j>.{from, to, points}} in dp; not valid for a cycle or a link to
// an id that is no node.
native void UiTreeLayout(aref nodes, aref edges, aref out);

// The object of each capability's screen, filled by the engine before its UiBuild and handed to its events:
// .{capability, screen (the screen file's stem), look (the screen entry's presentation), data (the binding's), model
// (the admitted widgets: <node>.{type, label, value, min, max, reason, offers.<verb>, context, headers, rows.r<index>})}.
object UiScreens;
