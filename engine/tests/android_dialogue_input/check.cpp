#include "android_dialog.hpp"
#include <algorithm>
#include <iostream>
#include <optional>

#include <catch2/catch.hpp>

using namespace storm::android;

// Fixtures for the game data the snapshot reads (dialog.cpp reads the script and string service).
const char *storm::android::gameLanguage()
{
    return "russian";
}

TEST_CASE("android_dialogue_input")
{
    REQUIRE(dialogueEditError("").empty());
    REQUIRE(dialogueEditError("  Проба: \\\"; Event(42) 😀  ").empty());
    for (const auto &invalid :
         {std::string("a\0b", 3), std::string("\n"), std::string("\r"), std::string("\t"), std::string("\x7f"),
          std::string("\xc2\x85"), std::string("\xe2\x80\xa8"), std::string("\xe2\x80\xa9"), std::string("\xc0\x80"),
          std::string("\xe0\x80\x80"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
          std::string("\xf0\x80\x80\x80"), std::string("\xf0\x9f"), std::string("\x80")})
        REQUIRE(dialogueEditError(invalid) == "invalid_text");
    REQUIRE(dialogueEditError(std::string(256, 'a')).empty());
    REQUIRE(dialogueEditError(std::string(257, 'a')) == "too_long");
    std::string emoji;
    for (int i = 0; i < 256; ++i)
        emoji += "😀";
    REQUIRE((emoji.size() == dialogueEditMaxBytes && dialogueEditError(emoji).empty()));
    REQUIRE(dialogueEditError(emoji + "a") == "too_long");

    ATTRIBUTES attrs, links;
    attrs.links = &links;
    attrs.values["Text"] = "Fixture question";
    attrs.values["CurrentNode"] = "start";
    auto link = std::make_unique<ATTRIBUTES>();
    link->value = "initial";
    link->values["go"] = "next";
    link->values["edit"] = "2";
    auto *choice = link.get();
    links.children.push_back(std::move(link));
    VDATA variable;
    core.variable = &variable;
    const void *owner = &attrs;
    const auto fits = [](std::string_view) { return true; };
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    {
        const storm::ui::Dialogue &shown = publications.back();
        REQUIRE((shown.active && shown.generation == dialogueGeneration && shown.language == "russian" &&
                 shown.speaker == "Fixture speaker" && shown.text == "Fixture question" && !shown.refusal));
        REQUIRE(shown.choices == std::vector<storm::ui::Dialogue::Choice>{{0, "initial", true}});
    }
    {
        // The same node again publishes nothing; a new text on it is a new dialogue state.
        const auto count = publications.size();
        const auto shown = dialogueGeneration;
        REQUIRE((publishDialogue(owner, &attrs, "Fixture speaker") && publications.size() == count &&
                 dialogueGeneration == shown));
        attrs.values["Text"] = "Fixture question, asked again";
        REQUIRE((publishDialogue(owner, &attrs, "Fixture speaker") && publications.size() == count + 1 &&
                 dialogueGeneration == shown + 1 && publications.back().text == "Fixture question, asked again"));
    }
    // The UI's reply as it sends it: a widget action of the dialogue's model, its opening the generation.
    uint64_t requests = 0;
    const auto send = [&](uint64_t generation, int index, std::optional<std::string> text) {
        storm::ui::Action action;
        action.screen = "dialogue";
        action.node = text ? "INPUT" : "REPLY";
        action.verb = text ? "edit" : "reply";
        action.value = text.value_or("");
        action.opening = generation;
        action.index = index;
        action.request = ++requests;
        dialogueActions.push_back(std::move(action));
        return requests;
    };
    // Every request is answered once: the last answer is that of `request`.
    const auto answered = [&](uint64_t request, bool applied) {
        return !ui::answers.empty() && ui::answers.back().request == request && ui::answers.back().applied == applied &&
               std::ranges::count_if(ui::answers, [&](const auto &a) { return a.request == request; }) == 1;
    };
    auto submit = [&](std::string text, auto width) {
        send(dialogueGeneration, 0, text);
        return takeDialogueChoice(owner, &attrs, width);
    };
    send(dialogueGeneration, 0, std::nullopt);
    REQUIRE(!takeDialogueChoice(owner, &attrs, fits)); // No accidental empty edit.
    REQUIRE(answered(requests, false));
    send(dialogueGeneration - 1, 0, "stale");
    REQUIRE((!takeDialogueChoice(owner, &attrs, fits) && answered(requests, false)));
    send(dialogueGeneration, 0, "wrong owner");
    REQUIRE((!takeDialogueChoice(&links, &attrs, fits) && answered(requests, false)));
    send(dialogueGeneration, 5, std::nullopt);
    REQUIRE((!takeDialogueChoice(owner, &attrs, fits) && answered(requests, false)));
    REQUIRE((variable.slots.empty() && !attrs.GetAttribute("value")));

    const auto generation = dialogueGeneration;
    REQUIRE(!submit("abc", [](std::string_view) { return false; }));
    REQUIRE((publications.back().refusal == storm::ui::Dialogue::Refusal{0, "too_long"} && answered(requests, false)));
    REQUIRE((publications.back().generation == generation && publications.back().choices.size() == 1));
    auto count = publications.size();
    REQUIRE(!submit("abc", [](std::string_view) { return false; }));
    REQUIRE((publications.size() == count + 1 && dialogueGeneration == generation));
    REQUIRE(!submit("bad\ntext", fits));
    REQUIRE(publications.back().refusal == storm::ui::Dialogue::Refusal{0, "invalid_text"});
    variable.writable = false;
    REQUIRE(!submit("ok", fits));
    REQUIRE(publications.back().refusal == storm::ui::Dialogue::Refusal{0, "unavailable"});
    REQUIRE((variable.slots.empty() && !attrs.GetAttribute("value")));
    core.variable = nullptr;
    REQUIRE(!submit("ok", fits));
    REQUIRE(publications.back().refusal == storm::ui::Dialogue::Refusal{0, "unavailable"});
    core.variable = &variable;
    variable.writable = true;
    const std::string text = "  Проба: \\\"; Event(42) 😀  ";
    const uint64_t taken = send(dialogueGeneration, 0, text);
    send(dialogueGeneration, 0, "duplicate");
    REQUIRE(takeDialogueChoice(owner, &attrs, fits) == choice);
    REQUIRE((variable.slots.at(2) == text && attrs.values.at("value") == text));
    REQUIRE((dialogueActions.empty() && answered(requests, false) &&
             ui::answers.at(ui::answers.size() - 2).request == taken &&
             ui::answers.at(ui::answers.size() - 2).applied)); // the taken one applied, the one after it refused

    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE(dialogueGeneration == generation + 1);
    choice->values["go"] = "same_caption_other_target";
    const auto old = dialogueGeneration;
    send(old, 0, "stale route");
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE((dialogueGeneration == old + 1 && dialogueActions.empty() && answered(requests, false)));
    attrs.values["CurrentNode"] = "other_node";
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE(dialogueGeneration == old + 2);
    choice->values["edit"] = "StRiNg";
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE(dialogueGeneration == old + 3);
    core.variable = nullptr;
    REQUIRE((submit("", fits) == choice && attrs.values.at("value").empty()));
    REQUIRE(variable.slots.at(2) == text);

    choice->values["edit"] = "10";
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE(submit("outside array", fits) == choice);
    REQUIRE((attrs.values.at("value") == "outside array" && variable.slots.size() == 1));

    auto last = std::make_unique<ATTRIBUTES>();
    last->values["go"] = "last_target";
    last->values["edit"] = "string";
    links.children.push_back(std::move(last));
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE(dialogueEditIndex(&links) == 1);
    REQUIRE(!submit("inactive edit", fits));
    send(dialogueGeneration, 0, std::nullopt);
    REQUIRE((takeDialogueChoice(owner, &attrs, fits) == choice && answered(requests, true)));
    links.children.pop_back();

    choice->values.erase("edit");
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE(!publications.back().choices.at(0).editable); // no typed answer
    REQUIRE(!submit("unexpected edit", fits));
    send(dialogueGeneration, 0, std::nullopt);
    REQUIRE((takeDialogueChoice(owner, &attrs, fits) == choice && answered(requests, true)));
    send(dialogueGeneration, 0, std::nullopt);
    closeDialogue(owner);
    REQUIRE(answered(requests, false)); // a reply the closed dialogue did not take
    REQUIRE((dialogueOwner == nullptr && publications.back() == storm::ui::Dialogue{}));
    // The UI's presentation gets the same dialogue flag as the activity, in the same call.
    REQUIRE((!storm::android::ui::presented.empty() && storm::android::ui::presented == env.shown &&
             !storm::android::ui::presented.back()));
    std::cout << "PASS UTF-8/input bounds, every reply answered once, the presentation's dialogue flag, actual bridge "
                 "metadata, stale "
                 "generations, errors, atomic edits and ordinary choices\n";
}
