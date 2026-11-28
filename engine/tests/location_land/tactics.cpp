// Officer orders (location/src/npc_tactics.hpp): the order decision and the target choice over invented situations.
#include "npc_tactics.hpp"

#include <catch2/catch.hpp>

#include <vector>

using namespace storm::location;

namespace
{
const TacticalTuning tuning;

OrderSituation Guard(const glm::vec3 &self, const glm::vec3 &anchor, bool combat)
{
    OrderSituation s;
    s.order = OfficerOrder::guard;
    s.combat = combat;
    s.self = self;
    s.anchor = anchor;
    s.radius = 7.0f;
    s.height = 1.5f;
    return s;
}
} // namespace

TEST_CASE("an order is hold or guard by name, in any case; any other string attacks")
{
    CHECK(ParseOfficerOrder("hold") == OfficerOrder::hold);
    CHECK(ParseOfficerOrder("Hold") == OfficerOrder::hold);
    CHECK(ParseOfficerOrder("GUARD") == OfficerOrder::guard);
    CHECK(ParseOfficerOrder("attack") == OfficerOrder::attack);
    CHECK(ParseOfficerOrder("holdx") == OfficerOrder::attack);
}

TEST_CASE("in peace a guard or attack order follows the guarded character, a hold order stays, weapons away")
{
    OrderState state;
    auto s = Guard({0, 0, 0}, {5, 0, 0}, false);
    auto d = DecideOrder(s, state, tuning);
    CHECK(d.action == OrderAction::follow);
    CHECK_FALSE(d.fightMode);
    s.order = OfficerOrder::attack;
    CHECK(DecideOrder(s, state, tuning).action == OrderAction::follow);
    s.order = OfficerOrder::hold;
    s.holdPoint = {20, 0, 0}; // far from the hold place: it still stays in peace
    d = DecideOrder(s, state, tuning);
    CHECK(d.action == OrderAction::stay);
    CHECK_FALSE(d.fightMode);
    s.order = OfficerOrder::guard;
    s.anchor.reset();
    CHECK(DecideOrder(s, state, tuning).action == OrderAction::stay);
}

TEST_CASE("a guard order in a fight fights a target inside the zone and follows when there is none")
{
    OrderState state;
    auto s = Guard({1, 0, 0}, {0, 0, 0}, true);
    s.hasTarget = true;
    auto d = DecideOrder(s, state, tuning);
    CHECK(d.action == OrderAction::fight);
    CHECK(d.fightMode);
    s.hasTarget = false;
    d = DecideOrder(s, state, tuning);
    CHECK(d.action == OrderAction::follow);
    CHECK(d.fightMode);
}

TEST_CASE("an officer that left the zone returns before fighting, until it is back within the inner margin")
{
    OrderState state;
    auto s = Guard({7.3f, 0, 0}, {0, 0, 0}, true); // within radius + outer margin (7.35): still inside
    s.hasTarget = true;
    CHECK(DecideOrder(s, state, tuning).action == OrderAction::fight);
    s.self = {7.4f, 0, 0}; // beyond it
    auto d = DecideOrder(s, state, tuning);
    CHECK(d.action == OrderAction::moveToCentre);
    CHECK(state.returning);
    s.self = {6.6f, 0, 0}; // inside the radius but not yet within radius - inner margin (6.5)
    CHECK(DecideOrder(s, state, tuning).action == OrderAction::moveToCentre);
    s.self = {6.5f, 0, 0};
    CHECK(DecideOrder(s, state, tuning).action == OrderAction::fight);
    CHECK_FALSE(state.returning);
    s.self = {0, 1.6f, 0}; // a floor above the centre is outside too
    CHECK(DecideOrder(s, state, tuning).action == OrderAction::moveToCentre);
}

TEST_CASE("the end of a fight forgets the return")
{
    OrderState state;
    auto s = Guard({9, 0, 0}, {0, 0, 0}, true);
    DecideOrder(s, state, tuning);
    CHECK(state.returning);
    s.combat = false;
    DecideOrder(s, state, tuning);
    CHECK_FALSE(state.returning);
}

TEST_CASE("an attack order has no zone: it fights wherever the target is")
{
    OrderState state;
    auto s = Guard({30, 0, 0}, {0, 0, 0}, true);
    s.order = OfficerOrder::attack;
    s.hasTarget = true;
    auto d = DecideOrder(s, state, tuning);
    CHECK(d.action == OrderAction::fight);
    CHECK_FALSE(state.returning);
    CHECK_FALSE(ZoneOf(s).active);
}

TEST_CASE("a hold order in a fight without a target goes to its place and stands there")
{
    OrderState state;
    OrderSituation s;
    s.order = OfficerOrder::hold;
    s.combat = true;
    s.anchor = glm::vec3(50, 0, 0); // the guarded character does not move the hold place
    s.holdPoint = {10, 0, 10};
    s.radius = 7.0f;
    s.height = 1.5f;
    s.self = {11, 0, 10}; // 1 m away: farther than the centre stop distance
    CHECK(DecideOrder(s, state, tuning).action == OrderAction::moveToCentre);
    s.self = {10.7f, 0, 10};
    CHECK(DecideOrder(s, state, tuning).action == OrderAction::stay);
    CHECK(ZoneOf(s).centre == glm::vec3(10, 0, 10));
}

TEST_CASE("the move to the centre is issued again only when it is not already heading there and may retry")
{
    const glm::vec3 centre(0, 0, 0);
    CHECK(NeedOrderMove(false, {}, centre, 0.0f, tuning));
    CHECK_FALSE(NeedOrderMove(false, {}, centre, 0.5f, tuning)); // a failed start waits
    CHECK_FALSE(NeedOrderMove(true, {0.9f, 0, 0}, centre, 0.0f, tuning));
    CHECK(NeedOrderMove(true, {1.1f, 0, 0}, centre, 0.0f, tuning)); // the centre moved: a new path
}

TEST_CASE("a guard order chooses who attacks the officer, then the guarded character, then whom it fights")
{
    OrderZone zone{{0, 0, 0}, 7.0f, 1.5f, true};
    std::vector<TargetCandidate> c(4);
    c[0] = {1, {1, 0, 0}};
    c[1] = {2, {3, 0, 0}, false, false, true};
    c[2] = {3, {4, 0, 0}, false, true, false};
    c[3] = {4, {5, 0, 0}, true, false, false};
    TargetMemory memory;
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning) == 3);
    c[3].attacksSelf = false;
    memory = {};
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning) == 2);
    c[2].attacksAnchor = false;
    memory = {};
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning) == 1);
    memory = {};
    CHECK(ChooseOrderTarget(c, {}, zone, false, memory, 0.0f, tuning) == 0); // other orders: the nearest
}

TEST_CASE("only enemies inside an active zone are chosen")
{
    OrderZone zone{{0, 0, 0}, 7.0f, 1.5f, true};
    std::vector<TargetCandidate> c = {{1, {8, 0, 0}, true}, {2, {0, 2, 0}, true}};
    TargetMemory memory;
    CHECK_FALSE(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning));
    zone.active = false;
    memory = {};
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning) == 1);
}

TEST_CASE("the target is kept between scans while it stays in the zone; a lost target is replaced at once")
{
    OrderZone zone{{0, 0, 0}, 7.0f, 1.5f, true};
    std::vector<TargetCandidate> c = {{1, {3, 0, 0}}, {2, {5, 0, 0}}};
    TargetMemory memory;
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning) == 0);
    c[1].position = {1, 0, 0}; // now nearer, but the scan interval has not run out
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.1f, tuning) == 0);
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.15f, tuning) == 1); // the next scan
    c[1].position = {8, 0, 0}; // the target left the zone: replaced before the interval runs out
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning) == 0);
    c.erase(c.begin() + 1); // the target died
    memory.id = 2;
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning) == 0);
}

TEST_CASE("between two equally good enemies the previous target stays")
{
    OrderZone zone{{0, 0, 0}, 7.0f, 1.5f, true};
    std::vector<TargetCandidate> c = {{1, {3, 0, 0}}, {2, {-3, 0, 0}}};
    TargetMemory memory{2, 0.0f};
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.0f, tuning) == 1);
}

TEST_CASE("after a scan that found no one the next scan waits for the interval")
{
    OrderZone zone{{0, 0, 0}, 7.0f, 1.5f, true};
    std::vector<TargetCandidate> none;
    TargetMemory memory;
    CHECK_FALSE(ChooseOrderTarget(none, {}, zone, true, memory, 0.0f, tuning));
    std::vector<TargetCandidate> c = {{1, {3, 0, 0}}};
    CHECK_FALSE(ChooseOrderTarget(c, {}, zone, true, memory, 0.1f, tuning));
    CHECK(ChooseOrderTarget(c, {}, zone, true, memory, 0.11f, tuning) == 0);
}
