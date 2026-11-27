// SPDX-License-Identifier: GPL-3.0-only
// The shared core of the touch interface's scripts, attached before every other one of them: the natives of the
// screens and the model the engine gives a screen's build.
#libriary "UiScripts"

native void UiShow(aref view);
native void UiPredict(aref view);
native void UiOn(string id, string verb);
native void UiSend(string node, string verb, string value);
native void UiBack(string node);
native void UiModal(string id, string cancel);
native int UiPending(string id);
native int UiOffers(string node, string verb);
native string UiText(string key);

// The admitted widgets of the screen being built, filled by the engine before each UiBuild (script_port.cpp
// FillModel): UiModel.<node>.{type, label, value, min, max, reason, offers.<verb>, context, headers, rows.r<index>}.
object UiModel;
