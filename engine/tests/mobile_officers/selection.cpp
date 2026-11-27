#include "mobile_officer_selection.hpp"
#include <iostream>

#include <catch2/catch.hpp>
using namespace storm::mobile;
TEST_CASE("mobile_officers")
{
    OfficerSelection selection;
    OfficerListIdentity state{12, "QA_skip", {"1\nHero", "42\nNavigator Folke", "0\nDoctor"}};
    selection.publish(state);
    auto action = selection.rowId(1);
    REQUIRE(selection.accepts(state, 1, action));
    REQUIRE(!selection.accepts(state, 0, action));
    REQUIRE(!selection.accepts(state, -1, action));
    REQUIRE(!selection.accepts(state, 3, action));
    REQUIRE(!selection.accepts(state, 1, "select"));
    auto changed = state;
    changed.revision++;
    REQUIRE(!selection.accepts(changed, 1, action));
    changed = state;
    changed.profile = "Other";
    REQUIRE(!selection.accepts(changed, 1, action));
    changed = state;
    changed.rows[1] = "43\nNavigator Other";
    REQUIRE(!selection.accepts(changed, 1, action));
    selection.publish(changed);
    REQUIRE(!selection.accepts(changed, 1, action));
    selection.publish(state); // List changed away and back: reject old event too.
    REQUIRE(!selection.accepts(state, 1, action));
    action = selection.rowId(1);
    selection.publish(state);
    REQUIRE(selection.accepts(state, 1, action));
    changed = state;
    changed.profile.clear();
    selection.publish(changed);
    REQUIRE(!selection.accepts(changed, 1, selection.rowId(1)));
    std::cout << "PASS officer selection identity, epoch, profile, replacement and bounds\n";
}
