// The application's settings as the settings screen offers them: what each one is (its values and their names,
// whether it applies at the next start, what it needs, why it cannot be set) is decided here, from the application's
// contract, the host's report (channel "appSettings") and the renderer's (channel "renderCaps"); the script screen
// lays the rows out and asks for changes. The player's changes are a draft of the screen's opening until «Готово».
#pragma once

#include "screen.h"

namespace storm::ui::screens
{

// What a script reads with UiState: {groups: [{id, title, tab, options: [{key, kind (segment | toggle), label,
// options [text], values [int], selected, on, restart, stack, disabled, reason, hint}]}], cache (absent without the
// host's report): {label, display, hint, disabled, reason, clear (the host can be asked to clear it)}}. Groups: map,
// screen, graphics, textures (absent for a game with its own texture maps), debug (a debuggable application only).
inline constexpr char kAppOptionsChannel[] = "appOptions";
Json AppOptions(Bindings &bindings);

// The screen opens: a new draft of the host's settings; the host measures the cache as it is now.
void OpenAppSettings(Bindings &bindings);
// The player sets `key` to `value`: one of the values the option offers now, else an error.
void SetAppSetting(Bindings &bindings, const std::string &key, int value);
// «Готово»: the draft goes to the host when it differs from what the host has.
void KeepAppSettings(Bindings &bindings);

} // namespace storm::ui::screens
