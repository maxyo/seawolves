// The journal in the touch UI (mobile_journal.hpp): the whole quest list by the scripts' index and id, the "open"
// action of the current screen revision only, and every record's full text.
#include "core_fake.hpp"
#include "mobile_journal.hpp"

#include <catch2/catch.hpp>

namespace
{
// The quest file reader: a title for every quest but "missing".
bool title(const char *log, const char *id, std::string &out)
{
    if (std::string(id) == "missing")
        return false;
    out = std::string(log) + " / Полный заголовок " + id;
    out.push_back('\0'); // the reader's explicit C terminator
    return true;
}
} // namespace

TEST_CASE("the whole quest list, opened by the scripts' row relative to the native top")
{
    storm::test::Codec codec;
    ATTRIBUTES quests(codec);
    for (int i = 0; i < 40; i++)
    {
        auto &quest = quests.CreateAttribute("quest" + std::to_string(i));
        quest.SetAttribute("LogName", "OtherLog");
        quest.SetAttribute("Complete", i % 2 ? "1" : "0");
    }
    quests.CreateAttribute("missing");
    MobileQuestList list;
    list.refresh("QUEST_TITLE", 100, &quests, 12, title);
    std::vector<MobileWidget> widgets;
    list.collect("QUEST_TITLE", 100, true, widgets);
    REQUIRE(widgets.size() == 40);
    CHECK(widgets.back().index == 39);
    CHECK(widgets.back().rowId == "quest39");
    CHECK(widgets.back().label == "OtherLog / Полный заголовок quest39");
    CHECK(widgets.back().value == "1");
    CHECK(widgets.back().order == 139);
    CHECK(widgets[0].actions == std::vector<std::string>{"open"});
    CHECK(list.opened(39, "open@17:quest39", 17) == 27);
    CHECK(list.opened(0, "open@17:quest0", 17) == -12);
    CHECK_FALSE(list.opened(39, "open@16:quest39", 17)); // another screen revision
    CHECK_FALSE(list.opened(39, "open@17:quest0", 17));  // another row's quest
    CHECK_FALSE(list.opened(-1, "press", 17));
    CHECK_FALSE(list.opened(90, "open@17:quest90", 17));
    widgets.clear();
    list.collect("QUEST_TITLE", 100, false, widgets);
    CHECK_FALSE(widgets[0].enabled);
    CHECK(widgets[0].actions.empty());
    list.clear(); // the node released
    widgets.clear();
    list.collect("QUEST_TITLE", 100, true, widgets);
    REQUIRE(widgets.size() == 1);
    CHECK(widgets[0].type == "text");
    CHECK_FALSE(list.opened(39, "open@17:quest39", 17));
}

TEST_CASE("no quests: one empty text")
{
    MobileQuestList list;
    list.refresh("QUEST_TITLE", 100, nullptr, 0, title);
    std::vector<MobileWidget> widgets;
    list.collect("QUEST_TITLE", 100, true, widgets);
    REQUIRE(widgets.size() == 1);
    CHECK(widgets[0].type == "text");
    CHECK(widgets[0].label.empty());
}

TEST_CASE("a record's paragraphs: one per line, a blank line between records")
{
    std::string text;
    MobileJournalAppend(text, {"18 октября 2026", "Новая запись целиком"});
    MobileJournalAppend(text, {});
    MobileJournalAppend(text, {"18 октября 2026", "Старая запись"});
    CHECK(text == "18 октября 2026\nНовая запись целиком\n\n18 октября 2026\nСтарая запись");
    std::string paragraph(std::string("Абзац") + '\0' + "хвост");
    std::string single;
    MobileJournalAppend(single, {paragraph});
    CHECK(single == "Абзац"); // a paragraph ends at its C terminator, as the game reads it
}
