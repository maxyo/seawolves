#pragma once
#ifdef STORM_TOUCH_UI
#include <platform/android_ui.hpp>
#include <platform/android_dialogue_input.hpp>
#include <core.h>
#include <string_compare.hpp>
#include <v_data.h>

#include "storm_ui/dialogue_state.h"

namespace storm::android
{
// Runtime dialogue data only; neither dialogue strings nor game files are bundled.
inline const void *dialogueOwner = nullptr;
inline storm::ui::Dialogue dialogueSnapshot; // what the dialogue shows; its generation is dialogueGeneration
inline std::string dialogueIdentity;
inline uint64_t dialogueGeneration = 0;
// The dialogue goes to the portable UI (the engine's UI host draws it); the activity only learns whether a
// dialogue covers the game (its presentation state and controls).
inline bool dialogueShown = false;
inline void sendDialogue(storm::ui::Dialogue state)
{
    const bool active = state.active;
    ui::dialogueState(std::move(state));
    if (active == dialogueShown)
        return;
    dialogueShown = active;
    ui::dialogue(active);
#ifdef __ANDROID__
    if (!CallActivity(upcall::GameActivity::showDialogue, [&](JNIEnv *env, jobject activity, jmethodID method) {
            env->CallVoidMethod(activity, method, static_cast<jboolean>(active));
            return true;
        }))
        spdlog::error("dialogue: GameActivity.showDialogue(boolean) failed");
#endif
}
inline void closeDialogue(const void *owner)
{
    if (dialogueOwner != owner)
        return;
    sendDialogue({});
    dialogueOwner = nullptr;
    dialogueSnapshot = {};
    dialogueIdentity.clear();
    refuseActions(dialogueActions, "the dialogue ended");
}
inline size_t dialogueEditIndex(ATTRIBUTES *links)
{
    // DlgLinkDescribe retains the last editable link when rebuilding a node.
    size_t index = size_t(-1);
    if (links)
        for (size_t i = 0; i < links->GetAttributesNum(); ++i)
        {
            auto *link = links->GetAttributeClass(i);
            if (link && link->GetAttribute("edit"))
                index = i;
        }
    return index;
}
inline std::string dialogueIdentityField(const char *value)
{
    if (!value)
        return "-:";
    const std::string_view text(value);
    return std::to_string(text.size()) + ':' + std::string(text);
}
// Game data read by the dialogue snapshot; defined in dialog.cpp (the host checks supply fixtures).
const char *gameLanguage(); // the string service's language, or nullptr
inline bool publishDialogue(const void *owner, ATTRIBUTES *attrs, const char *speaker)
{
    if (!attrs)
    {
        closeDialogue(owner);
        return false;
    }
    auto *links = attrs->GetAttributeClass("Links");
    const auto editIndex = dialogueEditIndex(links);
    // Hidden routing metadata participates in generation changes, even if all
    // displayed text is identical. It is never accepted from the Android UI.
    std::string identity = dialogueIdentityField(attrs->GetAttribute("CurrentNode"));
    // The game's language: the UI picks its own strings (buttons, editor) by it.
    const char *language = gameLanguage();
    const char *text = attrs->GetAttribute("Text");
    storm::ui::Dialogue body;
    body.active = true;
    body.language = language ? language : "";
    body.speaker = speaker ? speaker : "";
    body.text = text ? text : "";
    if (links)
        for (size_t i = 0; i < links->GetAttributesNum(); ++i)
        {
            auto *link = links->GetAttributeClass(i);
            identity += ',' + std::to_string(i) + ':';
            if (link)
                identity +=
                    dialogueIdentityField(link->GetAttribute("go")) + dialogueIdentityField(link->GetAttribute("edit"));
            if (!link || !link->GetAttribute("go") || !link->HasValue())
                continue;
            const bool editable = i == editIndex;
            body.choices.push_back({.index = static_cast<int>(i), .text = link->GetValue(), .editable = editable});
        }
    if (dialogueOwner == owner && dialogueSnapshot == body && dialogueIdentity == identity)
        return true;
    const auto generation = dialogueGeneration + 1;
    storm::ui::Dialogue state = body;
    state.generation = generation;
    sendDialogue(std::move(state));
    dialogueOwner = owner;
    dialogueSnapshot = std::move(body);
    dialogueIdentity = std::move(identity);
    dialogueGeneration = generation;
    refuseActions(dialogueActions, "the dialogue moved on to another node");
    return true;
}
// A typed answer refused: the dialogue state says why (every refusal, an identical retry's too), then the request.
inline void rejectDialogueEdit(const storm::ui::Action &action, std::string_view error)
{
    storm::ui::Dialogue state = dialogueSnapshot;
    state.generation = dialogueGeneration;
    state.refusal = storm::ui::Dialogue::Refusal{action.index, std::string(error)};
    sendDialogue(std::move(state));
    ui::answer(action.request, false, "typed answer refused: " + std::string(error));
}
// The reply the player chose (an Action of the dialogue's model: `opening` is the generation, `index` the link, verb
// "reply", or "edit" with the typed answer as its value), or none. Every request is answered: the one taken applied,
// the others refused with the reason.
template <class FitsWidth>
inline ATTRIBUTES *takeDialogueChoice(const void *owner, ATTRIBUTES *attrs, FitsWidth fitsWidth)
{
    while (!dialogueActions.empty())
    {
        const storm::ui::Action action = std::move(dialogueActions.front());
        dialogueActions.pop_front();
        const auto refuse = [&](const char *reason) { ui::answer(action.request, false, reason); };
        if (dialogueOwner != owner || action.opening != dialogueGeneration || !attrs)
        {
            refuse("a reply to a dialogue node no longer shown");
            continue;
        }
        auto *links = attrs->GetAttributeClass("Links");
        auto *link = links && action.index >= 0 && static_cast<size_t>(action.index) < links->GetAttributesNum()
                         ? links->GetAttributeClass(action.index)
                         : nullptr;
        if (!link || !link->GetAttribute("go") || !link->HasValue())
        {
            refuse("the node has no such reply");
            continue;
        }
        const bool editable = static_cast<size_t>(action.index) == dialogueEditIndex(links);
        if (action.verb != (editable ? "edit" : "reply"))
        {
            refuse(editable ? "the reply is a typed answer" : "the reply takes no typed answer");
            continue;
        }
        if (editable)
        {
            auto error = dialogueEditError(action.value);
            if (error.empty() && !fitsWidth(action.value))
                error = "too_long";
            if (!error.empty())
            {
                rejectDialogueEdit(action, error);
                continue;
            }
            const char *mode = link->GetAttribute("edit");
            const auto slot = link->GetAttributeAsDword("edit", 0);
            if (!storm::iEquals(mode, "string") && slot < 10)
            {
                auto *data = static_cast<VDATA *>(core.GetScriptVariable("dialogEditStrings"));
                if (!data || !data->Set(action.value.c_str(), slot))
                {
                    rejectDialogueEdit(action, "unavailable");
                    continue;
                }
            }
            attrs->SetAttribute("value", action.value);
        }
        ui::answer(action.request, true, {});
        refuseActions(dialogueActions, "another reply was taken first");
        dialogueSnapshot = {};
        dialogueIdentity.clear();
        return link;
    }
    return nullptr;
}
} // namespace storm::android
#endif
