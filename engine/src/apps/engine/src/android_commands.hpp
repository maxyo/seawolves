#pragma once
#ifdef __ANDROID__
#include "android_gameplay_state.hpp"
#include "platform/android_mailbox.hpp"
#include "platform/android_shortcuts.hpp"
#include "platform/android_ui_sink.hpp"

#include <cstdint>
#include <vector>

namespace storm::engine
{
// The commands of one loop iteration, in their order: the UI's (the HUD's typed, the game interfaces' text), then the
// activity's mailbox. `cancelEpoch` is the input cancel epoch read before this frame's commands were taken: a command
// the platform cancelled meanwhile (CancelInput on the activity's thread) does not act.
void DispatchCommands(const std::vector<android::ui::Command> &uiCommands,
                      const std::vector<android::mailbox::Message> &messages, android::GameplayState &gameplay,
                      android::GameplayShortcuts &shortcuts, android::QuickGame &quickGame, uint64_t cancelEpoch);
} // namespace storm::engine
#endif
