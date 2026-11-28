#pragma once
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "app_upcalls.hpp"
#include "storm_ui/action.h"
#include "storm_ui/dialogue_state.h"

// The production bridge is tested with in-memory delivery on the game thread: the dialogue
// state goes to the UI host (ui::dialogueState), the activity only learns whether a dialogue is shown.
using jobject = void *;
using jmethodID = void *;
using jboolean = unsigned char;
inline std::vector<storm::ui::Dialogue> publications;
struct JNIEnv
{
    void CallVoidMethod(jobject, jmethodID, jboolean active)
    {
        shown.push_back(active != 0);
    }
    std::vector<bool> shown;
};
inline JNIEnv env;
namespace storm::android
{
template <class Call> bool CallActivity(const JavaMethod &, Call &&call)
{
    return call(&env, nullptr, nullptr);
}
} // namespace storm::android
namespace storm::android::ui
{
struct Answer
{
    uint64_t request;
    bool applied;
    std::string reason;
};
inline std::vector<Answer> answers; // ui::answer, in order
inline void answer(uint64_t request, bool applied, std::string reason)
{
    answers.push_back({request, applied, std::move(reason)});
}
inline void dialogueState(storm::ui::Dialogue dialogue)
{
    publications.push_back(std::move(dialogue));
}
inline std::vector<bool> presented; // the presentation's dialogue flag
inline void dialogue(bool active)
{
    presented.push_back(active);
}
} // namespace storm::android::ui
namespace storm::android
{
inline std::deque<storm::ui::Action> dialogueActions;
inline void refuseActions(std::deque<storm::ui::Action> &queued, const char *reason)
{
    for (const auto &action : queued)
        ui::answer(action.request, false, reason);
    queued.clear();
}
} // namespace storm::android
namespace spdlog
{
template <typename... Args> void error(Args &&...)
{
}
} // namespace spdlog
