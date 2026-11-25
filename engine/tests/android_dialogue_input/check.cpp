#include "android_dialog.hpp"
#include <iostream>

#include <catch2/catch.hpp>

using namespace storm::android;

// Fixtures for the game data the snapshot reads (dialog.cpp reads the script and string service).
std::optional<int64_t> storm::android::dialogueHeroMoney()
{
    return 1500;
}
const char *storm::android::gameLanguage()
{
    return "russian";
}

TEST_CASE("android_dialogue_input")
{
    for (const auto malformed :
         {"", "1", ":1", "1:", "-1:0", "1:-1", "1:+2", "1:0x", "1 :0", "18446744073709551616:1", "1:4294967296"})
        REQUIRE(!parseDialogueInput(malformed));
    REQUIRE(!parseDialogueInput("1:2")->text);
    REQUIRE(parseDialogueInput("1:2:")->text->empty());
    REQUIRE(*parseDialogueInput("1:2:  x:y\\\"  ")->text == "  x:y\\\"  ");
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
        REQUIRE(shown.choices == std::vector<storm::ui::Dialogue::Choice>{{0, "initial", true, 256}});
        REQUIRE(shown.money == 1500);
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
    auto submit = [&](std::string text, auto width) {
        dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0:" + text);
        return takeDialogueChoice(owner, &attrs, width);
    };
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0");
    REQUIRE(!takeDialogueChoice(owner, &attrs, fits)); // No accidental empty edit.
    dialogueActions.push_back(std::to_string(dialogueGeneration - 1) + ":0:stale");
    REQUIRE(!takeDialogueChoice(owner, &attrs, fits));
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0:wrong owner");
    REQUIRE(!takeDialogueChoice(&links, &attrs, fits));
    REQUIRE((variable.slots.empty() && !attrs.GetAttribute("value")));

    const auto generation = dialogueGeneration;
    REQUIRE(!submit("abc", [](std::string_view) { return false; }));
    REQUIRE(publications.back().refusal == storm::ui::Dialogue::Refusal{0, "too_long"});
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
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0:" + text);
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0:duplicate");
    REQUIRE(takeDialogueChoice(owner, &attrs, fits) == choice);
    REQUIRE((variable.slots.at(2) == text && attrs.values.at("value") == text));
    REQUIRE(dialogueActions.empty());

    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE(dialogueGeneration == generation + 1);
    choice->values["go"] = "same_caption_other_target";
    const auto old = dialogueGeneration;
    dialogueActions.push_back(std::to_string(old) + ":0:stale route");
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE((dialogueGeneration == old + 1 && dialogueActions.empty()));
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
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0");
    REQUIRE(takeDialogueChoice(owner, &attrs, fits) == choice);
    links.children.pop_back();

    choice->values.erase("edit");
    REQUIRE(publishDialogue(owner, &attrs, "Fixture speaker"));
    REQUIRE((!publications.back().money && !publications.back().choices.at(0).editable)); // no typed answer
    REQUIRE(!submit("unexpected edit", fits));
    dialogueActions.push_back(std::to_string(dialogueGeneration) + ":0");
    REQUIRE(takeDialogueChoice(owner, &attrs, fits) == choice);
    closeDialogue(owner);
    REQUIRE((dialogueOwner == nullptr && publications.back() == storm::ui::Dialogue{}));
    // The UI's presentation gets the same dialogue flag as the activity, in the same call.
    REQUIRE((!storm::android::ui::presented.empty() && storm::android::ui::presented == env.shown &&
             !storm::android::ui::presented.back()));
    std::cout << "PASS UTF-8/input bounds, the presentation's dialogue flag, actual bridge metadata, stale "
                 "generations, errors, atomic edits and ordinary choices\n";
}
