// SPDX-License-Identifier: GPL-3.0-only
// The shared core of the touch interface's scripts, attached before every other one of them: the natives of the
// screens and the model the engine gives a screen's build.
#libriary "UiScripts"
// The capability ids TOUCH_CAP_<ID>: a define is the program's from its first segment on, so the core includes them
// once for every later one.
#include "interface/touch/capabilities.h"

native void UiShow(aref view);
native void UiPredict(aref view);
native void UiKeep();
native void UiOn(string id, string verb);
native void UiIgnore(string id, string verb);
native int UiBound(string id, string verb);
native void UiModal(string id, string cancel);
native int UiPending(string id);
native string UiText(string key);

// Widget actions. A row is the node's widget of that index (-1: the node's own); `guard` is the row identity the
// action must still find; UiSendAfter waits `seconds` and sends `lead` (empty: none) right before the value.
native void UiSend(string node, string verb, string value);
native void UiSendRow(string node, int row, string verb, string value, string guard);
native void UiSendAfter(string node, int row, string verb, string value, string guard, float seconds, string lead);
native int UiWaiting(string node, int row, string verb);
native void UiBack(string node);
native int UiOffers(string node, string verb);

// The build and the game's answers: UiAnsweredSince is 0 while waiting, 1 applied, 2 refused.
native void UiAwait();
native void UiRebuild();
native void UiRebuildAfter(float seconds);
native int UiFirst();
native int UiRevision();
native float UiNow();
native int UiRequestFloor();
native int UiAnsweredSince(int floor);

native void UiToast(string text);
native void UiRead(string title, string text);
native void UiError(string text);

// The figures of the game's texts, read as every screen reads them; `what` names the text in the error line.
native int UiAmount(string text, string what, int none);
native int UiHasAmount(string text);
native float UiNumber(string text, string what, float none);
native string UiDecimal(string text);
native string UiOneLine(string text);
native string UiFill(string text, string argument);

// The amount of a quantity control, the player's draft until he confirms it, kept by the engine under `id`.
native void UiDraftFollow(string id, string key, int origin, int other);
native void UiDraftClose(string id);
native int UiDraftOrigin(string id);
native int UiDraftOther(string id);
native int UiDraftValue(string id, int low, int high);
native void UiDraftBind(string id, string element, int low, int high, string reason, int grain);
native void UiDraftButton(aref out, string id, int low, int high, string more, string less, string sub, int disabled,
                          string reason, string commit);

// The object of each capability's screen, filled by the engine before its UiBuild and handed to its events:
// .{capability, screen (the screen file's stem), look (the screen entry's presentation), data (the binding's), model
// (the admitted widgets: <node>.{type, label, value, min, max, reason, offers.<verb>, context, headers, rows.r<index>})}.
object UiScreens;
