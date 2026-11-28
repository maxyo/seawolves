// The game's dialogue on its way to the UI host (ui_host/src/dialogue_relay.hpp, EngineUi::DialogueState and
// ReplayToHost): every state reaches the host, equal ones too, and a new host gets the newest.
#include "dialogue_relay.hpp"

#include <vector>

#include <catch2/catch.hpp>

using storm::android::ui::DialogueRelay;
using storm::ui::Dialogue;

namespace
{
struct FakeHost
{
    std::vector<Dialogue> given;
    void SetDialogue(const Dialogue &dialogue)
    {
        given.push_back(dialogue);
    }
};

Dialogue Speech(uint64_t generation, const char *text)
{
    Dialogue dialogue;
    dialogue.active = true;
    dialogue.generation = generation;
    dialogue.speaker = "Ростовщик";
    dialogue.text = text;
    dialogue.choices = {{0, "Сколько?", true}, {1, "Прощай."}};
    return dialogue;
}
} // namespace

TEST_CASE("every dialogue state reaches the host, an identical refusal twice as two calls")
{
    DialogueRelay relay;
    FakeHost host;
    relay.Send(&host, Speech(7, "Деньги в рост?"));
    Dialogue refused = Speech(7, "Деньги в рост?");
    refused.refusal = Dialogue::Refusal{0, "too_long"};
    relay.Send(&host, refused);
    relay.Send(&host, refused); // the player typed the same answer again: it is refused again and must show so
    REQUIRE(host.given.size() == 3);
    CHECK(host.given[1] == refused);
    CHECK(host.given[2] == refused);
}

TEST_CASE("a host that starts later gets the newest state, once")
{
    DialogueRelay relay;
    relay.Send<FakeHost>(nullptr, Speech(1, "Добрый день."));
    relay.Send<FakeHost>(nullptr, Speech(2, "Что вам угодно?"));
    FakeHost started;
    relay.Replay(started);
    REQUIRE(started.given.size() == 1);
    CHECK(started.given[0] == Speech(2, "Что вам угодно?"));
}

TEST_CASE("a new host after an old one gets the newest state the old one had")
{
    DialogueRelay relay;
    FakeHost old_host;
    relay.Send(&old_host, Speech(3, "Карты или кости?"));
    Dialogue ended; // the dialogue closed: inactive, the newest state
    relay.Send(&old_host, ended);
    FakeHost new_host;
    relay.Replay(new_host);
    REQUIRE(old_host.given.size() == 2);
    REQUIRE(new_host.given.size() == 1);
    CHECK(new_host.given[0] == ended);
}

TEST_CASE("no dialogue sent: a new host is given none")
{
    const DialogueRelay relay;
    FakeHost host;
    relay.Replay(host);
    CHECK(host.given.empty());
}
