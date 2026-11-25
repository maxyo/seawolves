#pragma once
#ifdef __ANDROID__
#include <platform/android_ui.hpp>
#include <platform/android_dialogue_input.hpp>
#include <core.h>
#include <string_compare.hpp>
#include <v_data.h>

#include "storm_ui/dialogue_state.h"

#include <cstdlib>
#include <optional>

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
    if (!CallActivity(upcall::GameActivity::showDialogue, [&](JNIEnv *env, jobject activity, jmethodID method) {
            env->CallVoidMethod(activity, method, static_cast<jboolean>(active));
            return true;
        }))
        spdlog::error("dialogue: GameActivity.showDialogue(boolean) failed");
}
inline void closeDialogue(const void *owner)
{
    if (dialogueOwner != owner)
        return;
    sendDialogue({});
    dialogueOwner = nullptr;
    dialogueSnapshot = {};
    dialogueIdentity.clear();
    dialogueActions.clear();
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
std::optional<int64_t> dialogueHeroMoney(); // the hero's money, if the script has it
const char *gameLanguage();                 // the string service's language, or nullptr
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
            body.choices.push_back({.index = static_cast<int>(i),
                                    .text = link->GetValue(),
                                    .editable = editable,
                                    .max_code_points = static_cast<int>(dialogueEditMaxCodePoints)});
        }
    // An answer the player types may be an amount (a bet, a deposit): the hero's money is
    // the upper bound the number panel offers. Absent when the script data has none.
    if (editIndex != size_t(-1))
        body.money = dialogueHeroMoney();
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
    dialogueActions.clear();
    return true;
}
inline void rejectDialogueEdit(unsigned index, std::string_view error)
{
    // Always acknowledge a valid submission failure, including identical retries.
    storm::ui::Dialogue state = dialogueSnapshot;
    state.generation = dialogueGeneration;
    state.refusal = storm::ui::Dialogue::Refusal{static_cast<int>(index), std::string(error)};
    sendDialogue(std::move(state));
}
template <class FitsWidth>
inline ATTRIBUTES *takeDialogueChoice(const void *owner, ATTRIBUTES *attrs, FitsWidth fitsWidth)
{
    while (!dialogueActions.empty())
    {
        auto request = std::move(dialogueActions.front());
        dialogueActions.pop_front();
        const auto input = parseDialogueInput(request);
        if (!input || dialogueOwner != owner || input->generation != dialogueGeneration || !attrs)
            continue;
        auto *links = attrs->GetAttributeClass("Links");
        if (!links || input->index >= links->GetAttributesNum())
            continue;
        auto *link = links->GetAttributeClass(input->index);
        if (!link || !link->GetAttribute("go") || !link->HasValue())
            continue;
        const bool editable = input->index == dialogueEditIndex(links);
        if (editable != input->text.has_value())
            continue;
        if (editable)
        {
            auto error = dialogueEditError(*input->text);
            if (error.empty() && !fitsWidth(*input->text))
                error = "too_long";
            if (!error.empty())
            {
                rejectDialogueEdit(input->index, error);
                continue;
            }
            const std::string text(*input->text);
            const char *mode = link->GetAttribute("edit");
            const auto slot = link->GetAttributeAsDword("edit", 0);
            if (!storm::iEquals(mode, "string") && slot < 10)
            {
                auto *data = static_cast<VDATA *>(core.GetScriptVariable("dialogEditStrings"));
                if (!data || !data->Set(text.c_str(), slot))
                {
                    rejectDialogueEdit(input->index, "unavailable");
                    continue;
                }
            }
            attrs->SetAttribute("value", text);
        }
        dialogueActions.clear();
        dialogueSnapshot = {};
        dialogueIdentity.clear();
        return link;
    }
    return nullptr;
}
} // namespace storm::android
#endif
