// The dynamic objects registry (location/src/dynamic_objects.cpp) over invented prefabs and placements.
#include "dynamic_objects.hpp"

#include "core_fake.hpp"

#include <catch2/catch.hpp>

#include <map>

using storm::location::DynamicObjectActor;
using storm::location::DynamicObjects;

namespace
{
// A desk the hero can use, a bench with two seats, a lamp without behaviour.
const std::map<std::string, std::string> kPrefabs{
    {"desk",
     R"({"behavior":{"components":{"interaction":{"enabled":true,"actions":{"use":{"enabled":true,"command":"","distance":1.5,"priority":3}}}}}})"},
    {"bench",
     R"({"behavior":{"components":{"hasseats":{"canSit":true,"sit":"Bench_Sit","enabled":true,"config":{"distance":3.0,"seats":{"a":{"name":"Left"},"b":{"name":"Right"}}}}}}})"},
    {"lamp", R"({"models":{"lamp":{}}})"},
};

struct Host final : storm::location::DynamicObjectsHost
{
    DATA hero{VAR_OBJECT};
    glm::vec3 heroAt{};
    bool offer = true;
    std::vector<std::string> offered; // owner ids the scripts were asked about
    std::vector<entid_t> deadModels;

    std::optional<std::string> ReadPrefab(const std::string &model) override
    {
        const auto found = kPrefabs.find(model);
        return found == kPrefabs.end() ? std::nullopt : std::optional(found->second);
    }
    bool ModelAlive(entid_t model) override
    {
        return std::ranges::find(deadModels, model) == deadModels.end();
    }
    std::optional<DynamicObjectActor> Player() override
    {
        return DynamicObjectActor{&hero, heroAt};
    }
    bool reachable = true;
    bool canSit = true;
    std::vector<std::string> checkedSeats;
    std::map<entid_t, std::vector<storm::location::ContainerLocator>> locators;

    std::optional<DynamicObjectActor> Actor(DATA *actor) override
    {
        return actor == &hero ? std::optional(DynamicObjectActor{&hero, heroAt, 1.7f}) : std::nullopt;
    }
    std::vector<storm::location::ContainerLocator> ModelLocators(entid_t model) override
    {
        return locators[model];
    }
    std::optional<float> ModelFloor(entid_t) override
    {
        return 0.0f;
    }
    bool Reachable(const DynamicObjectActor &actor, glm::vec3 &) override
    {
        REQUIRE(actor.variable == &hero);
        return reachable;
    }
    float Time() override
    {
        return 12.5f;
    }
    std::vector<std::string> calls; // the script functions the registry ran, in order
    bool handlerAccepts = true;
    std::optional<bool> Check(const char *function, ATTRIBUTES &, ATTRIBUTES &context) override
    {
        calls.emplace_back(function);
        if (std::string_view(function) == "DynamicObject_HasSeatsCanSit")
        {
            checkedSeats.push_back(context.GetAttribute("seatId"));
            return canSit;
        }
        if (std::string_view(function) == "DynamicObject_HasSeatsCanStand")
            return true;
        return handlerAccepts;
    }
    int transitionFrames = 0; // frames the scripts still advance a session
    std::optional<bool> UpdateSeatSession(DATA *actor, float) override
    {
        REQUIRE(actor == &hero);
        calls.emplace_back("UpdateActor");
        return transitionFrames-- > 0;
    }
    std::optional<bool> CanOfferInteractionAt(ATTRIBUTES &owner, const std::string &, const DynamicObjectActor &actor,
                                              ATTRIBUTES &) override
    {
        REQUIRE(actor.variable == &hero);
        offered.push_back(owner.GetAttribute("id"));
        return offer;
    }
};

storm::location::ObjectLibrary Library()
{
    return storm::location::ParseObjectLibrary(R"({"Desk_01":"desk","Bench_01":"bench","Lamp_01":"lamp"})");
}

storm::location::ObjectInstance At(const char *model, const char *group, const char *locator, glm::vec3 position)
{
    glm::mat4 transform(1.0f);
    transform[3] = glm::vec4(position, 1.0f);
    return {model, group, locator, transform};
}

std::string Text(ATTRIBUTES &attributes, const char *path)
{
    ATTRIBUTES *found = attributes.FindAClass(&attributes, path);
    return found && found->HasValue() ? found->GetValue() : std::string("<none>");
}

constexpr const char *kContainer = "locations\\test\\hall_object";

void SetVectorOf(ATTRIBUTES &data, const glm::vec3 &at)
{
    ATTRIBUTES &position = *data.CreateSubAClass(&data, "actorPosition");
    position.SetAttributeUseFloat("x", at.x);
    position.SetAttributeUseFloat("y", at.y);
    position.SetAttributeUseFloat("z", at.z);
}
} // namespace

TEST_CASE("a placement's id is its UUID locator, else the same derived UUID in every session")
{
    CHECK(storm::location::DynamicObjectId("Test_Hall", kContainer, "Desk_01", "l1") ==
          "e24d5ea9-9a7c-8732-bd10-4bda26dbc0d7");
    CHECK(storm::location::DynamicObjectId("test_hall", "locations/test/hall_object", "desk_01", "L1") ==
          "e24d5ea9-9a7c-8732-bd10-4bda26dbc0d7");
    CHECK(storm::location::DynamicObjectId("Other_Hall", kContainer, "Desk_01", "l1") !=
          storm::location::DynamicObjectId("Test_Hall", kContainer, "Desk_01", "l1"));

    storm::test::Codec codec;
    Host host;
    const auto library = Library();
    DynamicObjects objects(codec, "Test_Hall", library, host);
    objects.AddPlacement(kContainer, At("desk", "Desk_01", "0F0E6E3A-1111-4222-8333-944455556666", {}), entid_t{1});
    objects.AddPlacement(kContainer, At("desk", "Desk_01", "l1", {}), entid_t{2});
    objects.AddPlacement(kContainer, At("desk", "Desk_01", "l1", {}), entid_t{3}); // the same id again: left out
    CHECK(objects.Count() == 2);
    CHECK(objects.Record("0f0e6e3a-1111-4222-8333-944455556666", nullptr) != nullptr);
    CHECK(objects.Record("e24d5ea9-9a7c-8732-bd10-4bda26dbc0d7", nullptr) != nullptr);
}

TEST_CASE("an instance's record has its place and its prefab's behaviour without the prefab's definition")
{
    storm::test::Codec codec;
    Host host;
    const auto library = Library();
    DynamicObjects objects(codec, "Test_Hall", library, host);
    objects.AddPlacement(kContainer, At("bench", "Bench_01", "l1", {1.0f, 2.0f, 3.0f}), entid_t{1});
    ATTRIBUTES *record =
        objects.Record(storm::location::DynamicObjectId("Test_Hall", kContainer, "Bench_01", "l1"), nullptr);
    REQUIRE(record != nullptr);
    CHECK(Text(*record, "prefab") == "Bench_01");
    CHECK(Text(*record, "model") == "bench");
    CHECK(Text(*record, "location") == "Test_Hall");
    CHECK(Text(*record, "position.z") == "3");
    CHECK(Text(*record, "loaded") == "1");
    CHECK(Text(*record, "components.hasseats.sit") == "Bench_Sit");
    CHECK(record->FindAClass(record, "components.hasseats.config") == nullptr);
    CHECK(record->FindAClass(record, "components.hasseats.state.seats") != nullptr);
    CHECK(record->FindAClass(record, "components.lifetime") == nullptr);
    CHECK(Text(*record, "components.interaction.actions.sit.command") == "hasseats.sit");
    CHECK(Text(*record, "components.interaction.actions.sit.distance") == "3");
    CHECK(Text(*record, "components.interaction.actions.stand.targetRange") == "1");

    ATTRIBUTES prefab(codec);
    prefab.SetAttribute("prefab", std::string_view("bench_01"));
    REQUIRE(objects.Request("prefab", prefab, nullptr) == 1);
    CHECK(Text(prefab, "components.hasseats.config.seats.b.name") == "Right");
    CHECK(Text(prefab, "components.hasseats.config.animations.0") == "Sit_Bench_Idle01");
    prefab.SetAttribute("prefab", std::string_view("Nothing"));
    CHECK(objects.Request("prefab", prefab, nullptr) == 0);
    CHECK(Text(prefab, "error") == "prefab_not_found");
}

TEST_CASE("the configure native sets the lifetime of a base type and keeps it for any other")
{
    storm::test::Codec codec;
    ATTRIBUTES instance(codec);
    storm::location::ConfigureDynamicObjectComponents(instance, 1);
    CHECK(Text(instance, "components.lifetime.config.mode") == "days");
    storm::location::ConfigureDynamicObjectComponents(instance, -1);
    CHECK(Text(instance, "components.lifetime.config.mode") == "days");
    storm::location::ConfigureDynamicObjectComponents(instance, 4);
    CHECK(Text(instance, "components.lifetime.config.mode") == "load");
    CHECK(Text(instance, "components.lifetime.enabled") == "1");
}

TEST_CASE("the interaction target is the nearest instance with an offered action")
{
    storm::test::Codec codec;
    Host host;
    const auto library = Library();
    DynamicObjects objects(codec, "Test_Hall", library, host);
    objects.AddPlacement(kContainer, At("lamp", "Lamp_01", "l1", {0.2f, 0.0f, 0.0f}), entid_t{1});
    objects.AddPlacement(kContainer, At("desk", "Desk_01", "l1", {1.0f, 0.0f, 0.0f}), entid_t{2});
    objects.AddPlacement(kContainer, At("desk", "Desk_01", "l2", {-1.0f, 0.0f, 0.0f}), entid_t{3});
    const std::string right = storm::location::DynamicObjectId("Test_Hall", kContainer, "Desk_01", "l1");
    const std::string left = storm::location::DynamicObjectId("Test_Hall", kContainer, "Desk_01", "l2");

    ATTRIBUTES data(codec);
    host.heroAt = {0.0f, 0.0f, 0.0f}; // as near to both desks: the first placed wins
    REQUIRE(objects.Request("interaction", data, nullptr) == 1);
    CHECK(Text(data, "ownerId") == right);
    CHECK(Text(data, "count") == "1");
    CHECK(Text(data, "items.item0.action") == "use");
    CHECK(Text(data, "items.item0.behavior") == "interaction");
    CHECK(Text(data, "items.item0.priority") == "3");

    host.heroAt = {-0.5f, 0.0f, 0.0f};
    REQUIRE(objects.Request("interaction", data, nullptr) == 1);
    CHECK(Text(data, "ownerId") == left);

    host.deadModels = {3}; // its container went: the other desk
    REQUIRE(objects.Request("interaction", data, nullptr) == 1);
    CHECK(Text(data, "ownerId") == right);
    host.deadModels.clear();

    host.heroAt = {-3.0f, 0.0f, 0.0f}; // beyond the action's distance of both
    CHECK(objects.Request("interaction", data, nullptr) == 0);
    CHECK(Text(data, "ownerId").empty());

    host.heroAt = {-0.5f, 0.0f, 0.0f};
    host.offer = false; // the scripts refuse
    host.offered.clear();
    CHECK(objects.Request("interaction", data, nullptr) == 0);
    CHECK(host.offered == std::vector<std::string>{right, left});
}

TEST_CASE("requests by record or id, and a request this engine does not have")
{
    storm::test::Codec codec;
    Host host;
    const auto library = Library();
    DynamicObjects objects(codec, "Test_Hall", library, host);
    objects.AddPlacement(kContainer, At("desk", "Desk_01", "l1", {4.0f, 5.0f, 6.0f}), entid_t{7});
    const std::string id = storm::location::DynamicObjectId("Test_Hall", kContainer, "Desk_01", "l1");
    ATTRIBUTES *record = objects.Record(id, nullptr);

    ATTRIBUTES data(codec);
    REQUIRE(objects.Request("get", data, record) == 1);
    CHECK(Text(data, "id") == id);
    CHECK(Text(data, "position.y") == "5");
    CHECK(objects.Request("exists", data, record) == 1);
    CHECK(objects.Entity("", record, &data) == entid_t{7});
    CHECK(objects.Request("library", data, nullptr) == 1);
    CHECK(Text(data, "count") == "3");

    objects.RemoveModels({7});
    CHECK(objects.Count() == 0);
    ATTRIBUTES missing(codec);
    missing.SetAttribute("id", std::string_view(id));
    CHECK(objects.Request("exists", missing, nullptr) == 0);
    CHECK(Text(missing, "error").empty());
    CHECK(objects.Request("get", missing, nullptr) == 0);
    CHECK(Text(missing, "error") == "object_not_found");

    CHECK(objects.Request("save_authoring", data, nullptr) == 0);
    CHECK(Text(data, "error") == "unknown_operation");
}

TEST_CASE("a bench offers its nearest free seat the hero can walk to")
{
    storm::test::Codec codec;
    Host host;
    const auto library = Library();
    DynamicObjects objects(codec, "Test_Hall", library, host);
    const auto seat = [](const char *name, float x) {
        glm::mat4 local(1.0f);
        local[3] = glm::vec4(x, 0.5f, 0.0f, 1.0f);
        return storm::location::ContainerLocator{"seats", name, local};
    };
    host.locators[entid_t{1}] = {seat("a", -0.5f), seat("b", 0.5f)};
    objects.AddPlacement(kContainer, At("bench", "Bench_01", "l1", {}), entid_t{1});

    ATTRIBUTES data(codec);
    host.heroAt = {-0.6f, 0.0f, 1.0f};
    REQUIRE(objects.Request("interaction", data, nullptr) == 1);
    CHECK(Text(data, "count") == "1");
    CHECK(Text(data, "items.item0.command") == "hasseats.sit");
    CHECK(Text(data, "items.item0.seatId") == "a");
    CHECK(Text(data, "items.item0.seatName") == "Left");

    host.checkedSeats.clear();
    host.canSit = false; // the scripts refuse the nearer seat only
    host.heroAt = {-0.6f, 0.0f, 1.0f};
    CHECK(objects.Request("interaction", data, nullptr) == 0);
    CHECK(host.checkedSeats == std::vector<std::string>{"a", "b"});
    host.canSit = true;

    host.reachable = false;
    CHECK(objects.Request("interaction", data, nullptr) == 0);
    host.reachable = true;

    host.heroAt = {0.0f, 0.0f, 3.6f}; // beyond the seats' distance of 3: the scripts are asked about standing only
    host.offered.clear();
    CHECK(objects.Request("interaction", data, nullptr) == 0);
    CHECK(host.offered.size() == 1);
    ATTRIBUTES far(codec);
    far.CreateAttribute("actor").SetReference(&host.hero);
    CHECK(objects.Request("seats_can_sit", far,
                          objects.Record(storm::location::DynamicObjectId("Test_Hall", kContainer, "Bench_01", "l1"),
                                         nullptr)) == 0);
    CHECK(Text(far, "error") == "no_suitable_slot");

    // The context the seat check gives the scripts: the seat, the point in front of it on the floor, the handlers.
    ATTRIBUTES context(codec);
    context.CreateAttribute("actor").SetReference(&host.hero);
    context.SetAttribute("seatId", std::string_view("b"));
    host.heroAt = {0.0f, 0.0f, 1.0f};
    REQUIRE(objects.Request("seats_can_sit", context,
                            objects.Record(storm::location::DynamicObjectId("Test_Hall", kContainer, "Bench_01", "l1"),
                                           nullptr)) == 1);
    CHECK(Text(context, "position.x") == "0.5");
    CHECK(Text(context, "approach.position.x") == "0.5");
    CHECK(Text(context, "approach.position.y") == "0");
    CHECK(Text(context, "approach.position.z") == "0.8");
    CHECK(Text(context, "sitStart.position.z") == "0.25");
    CHECK(Text(context, "approach.reachable") == "1");
    CHECK(Text(context, "approach.heightTolerance") == "1.7"); // the sitting character's height
    CHECK(Text(context, "sit") == "Bench_Sit");
    CHECK(Text(context, "animations.0") == "Sit_Bench_Idle01");
    CHECK(context.GetAttributeClass("actor")->GetReference() == &host.hero);
}

TEST_CASE("the hero takes a seat, is offered to stand up, and the seat is his until the scripts release it")
{
    storm::test::Codec codec;
    Host host;
    const auto library = Library();
    DynamicObjects objects(codec, "Test_Hall", library, host);
    glm::mat4 local(1.0f);
    local[3] = glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f);
    host.locators[entid_t{1}] = {{"seats", "a", local}};
    objects.AddPlacement(kContainer, At("bench", "Bench_01", "l1", {}), entid_t{1});
    objects.AddPlacement(kContainer, At("desk", "Desk_01", "l1", {5.0f, 0.0f, 5.5f}), entid_t{2});
    ATTRIBUTES *bench =
        objects.Record(storm::location::DynamicObjectId("Test_Hall", kContainer, "Bench_01", "l1"), nullptr);
    host.heroAt = {-0.5f, 0.0f, 1.0f};
    const auto context = [&](const char *seat) {
        ATTRIBUTES data(codec);
        data.CreateAttribute("actor").SetReference(&host.hero);
        data.SetAttribute("seatId", std::string_view(seat));
        return data;
    };

    // The offer goes to the scripts' interaction, which the engine runs with the chosen seat.
    ATTRIBUTES offers(codec);
    REQUIRE(objects.Request("interaction", offers, nullptr) == 1);
    ATTRIBUTES execute(codec);
    execute.CreateAttribute("selection") = offers.GetAttributeClass("items")->GetAttributeClass("item0")->Copy();
    execute.CreateAttribute("actor").SetReference(&host.hero);
    SetVectorOf(execute, host.heroAt);
    host.calls.clear();
    REQUIRE(objects.Request("execute_interaction", execute, bench) == 1);
    CHECK(host.calls.back() == "DynamicObject_RunInteraction");
    CHECK(Text(*bench, "busy") == "0");
    execute.GetAttributeClass("selection")->SetAttribute("command", std::string_view("hasseats.stand"));
    CHECK(objects.Request("execute_interaction", execute, bench) == 0);
    CHECK(Text(execute, "error") == "stale_selection");

    // The scripts' handler refuses: the seat stays free.
    host.handlerAccepts = false;
    ATTRIBUTES refused = context("a");
    refused.SetAttribute("command", std::string_view("hasseats.sit"));
    CHECK(objects.Request("command", refused, bench) == 0);
    CHECK(Text(refused, "status") == "rejected");
    CHECK(Text(refused, "error") == "handler_rejected");
    ATTRIBUTES query = context("a");
    CHECK(objects.Request("seat_query", query, bench) == 0);
    host.handlerAccepts = true;

    ATTRIBUTES sit = context("a");
    sit.SetAttribute("command", std::string_view("hasseats.sit"));
    REQUIRE(objects.Request("command", sit, bench) == 1);
    CHECK(Text(sit, "status") == "completed");
    CHECK(objects.Request("seat_query", query, bench) == 1);
    ATTRIBUTES seats(codec);
    REQUIRE(objects.Request("seats_describe", seats, bench) == 1);
    CHECK(Text(seats, "item0.occupied") == "1");
    ATTRIBUTES again = context("a");
    CHECK(objects.Request("seats_can_sit", again, bench) == 0); // taken, and he sits already

    // Seated, he is offered to stand up, even beside a nearer desk.
    host.heroAt = {5.0f, 0.0f, 5.0f};
    REQUIRE(objects.Request("interaction", offers, nullptr) == 1);
    CHECK(Text(offers, "count") == "1");
    CHECK(Text(offers, "items.item0.command") == "hasseats.stand");

    // The scripts advance the session while it is in a transition, then no more.
    host.transitionFrames = 1;
    host.calls.clear();
    objects.Tick();
    objects.Tick();
    objects.Tick();
    CHECK(host.calls == std::vector<std::string>{"UpdateActor", "UpdateActor"});

    ATTRIBUTES stand = context("a");
    stand.SetAttribute("command", std::string_view("hasseats.stand"));
    REQUIRE(objects.Request("command", stand, bench) == 1);
    CHECK(objects.Request("seat_query", query, bench) == 1); // standing up keeps the seat until it is released
    ATTRIBUTES release = context("a");
    CHECK(objects.Request("seat_release", release, bench) == 1);
    CHECK(objects.Request("seat_query", query, bench) == 0);
    host.heroAt = {-0.5f, 0.0f, 1.0f};
    REQUIRE(objects.Request("interaction", offers, nullptr) == 1);
    CHECK(Text(offers, "items.item0.command") == "hasseats.sit");
}

TEST_CASE("only a character becomes the actor of a request: a value on the script stack is refused")
{
    storm::test::Codec codec;
    ATTRIBUTES context(codec);
    DATA character{VAR_OBJECT};
    DATA text{VAR_STRING}; // what `session.actor` reads as once its reference is gone, a stack temporary
    CHECK(!storm::location::SetDynamicObjectActor(context, &text));
    CHECK(Text(context, "error") == "actor_unavailable");
    CHECK(context.GetAttributeClass("actor") == nullptr);
    CHECK(!storm::location::SetDynamicObjectActor(context, nullptr));
    REQUIRE(storm::location::SetDynamicObjectActor(context, &character));
    CHECK(context.GetAttributeClass("actor")->GetReference() == &character);
}
