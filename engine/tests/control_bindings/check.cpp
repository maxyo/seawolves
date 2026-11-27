#include "control_bindings.hpp"

#include <catch2/catch.hpp>

#include <set>

using namespace storm::controls;

namespace
{
constexpr int32_t kFight = 8, kLand = 9;
constexpr int32_t kMouseLeft = 1, kKeyQ = 'Q';
enum : int32_t
{
    Fast,
    Round,
    Other,
    kControls
};

struct Keys
{
    std::set<int32_t> down;
    ControlBindings::KeyDown Down() const
    {
        return [this](int32_t key) { return down.contains(key); };
    }
};

// The fight group's left button: a tap is a fast attack, a hold a round one (Caribbean Legend's new combat scheme).
ControlBindings FightButton(const Keys &keys)
{
    ControlBindings bindings;
    bindings.Resize(kControls);
    bindings.Map(Fast, kMouseLeft, kFight);
    bindings.Map(Round, kMouseLeft, kFight);
    bindings.SetHold(Round, kFight, true);
    REQUIRE(bindings.Pair(Fast, Round));
    bindings.SetThreshold(200);
    bindings.SetGroup(kFight);
    bindings.Rebuild(keys.Down());
    return bindings;
}
} // namespace

TEST_CASE("control_bindings.groups")
{
    ControlBindings bindings;
    bindings.Resize(kControls);
    bindings.Map(Other, kKeyQ, kAllGroups);
    bindings.Map(Other, kMouseLeft, kFight);
    REQUIRE(bindings.Key(Other) == kKeyQ);
    bindings.SetGroup(kFight);
    REQUIRE(bindings.Key(Other) == kMouseLeft);
    bindings.SetGroup(kLand);
    REQUIRE(bindings.Key(Other) == kKeyQ);
    bindings.Map(Other, kNoKey, kAllGroups);
    REQUIRE(bindings.Key(Other) == kNoKey);
    REQUIRE(bindings.State(Other) == Pulse::Ordinary);
}

TEST_CASE("control_bindings.tap")
{
    Keys keys;
    ControlBindings bindings = FightButton(keys);
    keys.down = {kMouseLeft};
    bindings.Update(1000, keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Inactive); // undecided while held under the threshold
    bindings.Update(1150, keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Inactive);
    keys.down.clear();
    bindings.Update(1180, keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Activated);
    REQUIRE(bindings.State(Round) == Pulse::Inactive);
    bindings.Update(1200, keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Inactivated);
    bindings.Update(1220, keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Inactive);
}

TEST_CASE("control_bindings.hold")
{
    Keys keys;
    ControlBindings bindings = FightButton(keys);
    keys.down = {kMouseLeft};
    bindings.Update(1000, keys.Down());
    bindings.Update(1199, keys.Down());
    REQUIRE(bindings.State(Round) == Pulse::Inactive);
    bindings.Update(1200, keys.Down()); // still held: fires at the threshold
    REQUIRE(bindings.State(Round) == Pulse::Activated);
    bindings.Update(1400, keys.Down());
    REQUIRE(bindings.State(Round) == Pulse::Inactivated);
    keys.down.clear();
    bindings.Update(1500, keys.Down()); // the release of a hold is no tap
    REQUIRE(bindings.State(Fast) == Pulse::Inactive);
    REQUIRE(bindings.State(Round) == Pulse::Inactive);
}

TEST_CASE("control_bindings.held_at_rebuild")
{
    // A button already down when the group changes acts only after it is released and pressed again.
    Keys keys;
    keys.down = {kMouseLeft};
    ControlBindings bindings = FightButton(keys);
    bindings.Update(1000, keys.Down());
    bindings.Update(2000, keys.Down());
    REQUIRE(bindings.State(Round) == Pulse::Inactive);
    keys.down.clear();
    bindings.Update(2100, keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Inactive);
    keys.down = {kMouseLeft};
    bindings.Update(2200, keys.Down());
    bindings.Update(2400, keys.Down());
    REQUIRE(bindings.State(Round) == Pulse::Activated);
}

TEST_CASE("control_bindings.other_group")
{
    // Out of the fight group the button is an ordinary key again.
    Keys keys;
    ControlBindings bindings = FightButton(keys);
    bindings.Map(Fast, kMouseLeft, kLand);
    bindings.SetGroup(kLand);
    bindings.Rebuild(keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Ordinary);
    REQUIRE(bindings.Key(Round) == kNoKey);
}

TEST_CASE("control_bindings.ambiguous")
{
    // An unpaired hold control on a key two other controls share is left to the ordinary path.
    Keys keys;
    ControlBindings bindings;
    bindings.Resize(kControls);
    for (const int32_t c : {Fast, Round, Other})
        bindings.Map(c, kKeyQ, kAllGroups);
    bindings.SetHold(Round, kAllGroups, true);
    bindings.Rebuild(keys.Down());
    REQUIRE(bindings.State(Round) == Pulse::Ordinary);
    bindings.Map(Other, kNoKey, kAllGroups);
    bindings.Rebuild(keys.Down());
    REQUIRE(bindings.State(Round) == Pulse::Inactive);
    REQUIRE(bindings.State(Fast) == Pulse::Inactive);
}

TEST_CASE("control_bindings.unholdable")
{
    // The wheel cannot be held: a hold control on it stays an ordinary key.
    constexpr int32_t kWheelUp = kHoldableKeys;
    Keys keys;
    ControlBindings bindings;
    bindings.Resize(kControls);
    bindings.Map(Round, kWheelUp, kAllGroups);
    bindings.SetHold(Round, kAllGroups, true);
    bindings.Rebuild(keys.Down());
    REQUIRE(bindings.State(Round) == Pulse::Ordinary);
}

TEST_CASE("control_bindings.pairs")
{
    ControlBindings bindings;
    bindings.Resize(kControls);
    REQUIRE(!bindings.Pair(Fast, Fast));
    REQUIRE(!bindings.Pair(Fast, kControls));
    REQUIRE(bindings.Pair(-1, -1));
}

TEST_CASE("control_bindings.schemes")
{
    Keys keys;
    ControlBindings bindings;
    bindings.Resize(kControls);
    bindings.Map(Fast, kMouseLeft, kAllGroups);
    bindings.SetScheme(Fast, 2);
    REQUIRE(bindings.Key(Fast) == kNoKey); // the classic scheme (1) is current
    REQUIRE(bindings.State(Fast) == Pulse::Inactive);
    keys.down = {kMouseLeft};
    REQUIRE(bindings.SetCurrentScheme(2, keys.Down()));
    REQUIRE(bindings.Key(Fast) == kMouseLeft);
    bindings.Update(1000, keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Inactive); // held through the switch
    keys.down.clear();
    bindings.Update(1100, keys.Down());
    REQUIRE(bindings.State(Fast) == Pulse::Ordinary);
}
