#include "mobile_owner_selection.hpp"
#include <iostream>
#include <map>

#include <catch2/catch.hpp>
class Codec final : public VSTRING_CODEC
{
    std::map<uint32_t, std::string> values;

  public:
    uint32_t GetNum() override
    {
        return values.size();
    }
    uint32_t Convert(const char *value) override
    {
        return Convert(value, std::char_traits<char>::length(value));
    }
    uint32_t Convert(const char *value, int32_t length) override
    {
        const std::string text(value, length);
        for (const auto &entry : values)
            if (entry.second == text)
                return entry.first;
        const auto code = uint32_t(values.size() + 1);
        values[code] = text;
        return code;
    }
    const char *Convert(uint32_t code) override
    {
        return values.at(code).c_str();
    }
    std::optional<uint32_t> Find(std::string_view name) const override
    {
        for (const auto &entry : values)
            if (entry.second == name)
                return entry.first;
        return std::nullopt;
    }
    void VariableChanged() override
    {
    }
};

TEST_CASE("mobile_owners")
{
    using namespace storm::mobile;
    Codec codec;
    ATTRIBUTES attrs(codec);
    // Stock FillPassengerScroll: eligible passenger 7 followed by a rejected
    // passenger (prisoned, not removable, occupied or already holding this post).
    // makearef runs BEFORE those restrictions; m only advances for eligible rows.
    // Thus the output is pic1.character=7, empty pic2, absent pic3, ListSize=m+2=3.
    // Literal producer output, independent of OwnerRows' padding predicate.
    {
        ATTRIBUTES chooser(codec);
        chooser.SetAttribute("ListSize", "3");
        chooser.SetAttribute("current", "0");
        auto *eligible = chooser.CreateAttribute("pic1", nullptr);
        eligible->SetAttribute("character", "7");
        auto *padding = chooser.CreateAttribute("pic2", nullptr);
        OfficerListIdentity identity;
        std::vector<MobileWidget> choices;
        OfficerSelection lease;
        auto collectChooser = [&](const char *screen = "lscchar_all", uint64_t revision = 23,
                                  const char *profile = "QA") {
            return OwnerRows(
                screen, "PASSENGERSLIST", false, &chooser, 4, revision, profile, true, 10,
                [](uint32_t id, bool) { return id == 7 || id == 8 ? "person:" + std::to_string(id) : std::string(); },
                "", identity, choices);
        };
        for (const char *screen : {"character", "lscchar_all"})
        {
            REQUIRE(collectChooser(screen)); // regression: old exporter drops candidate 0
            REQUIRE((choices.size() == 1 && choices[0].index == 0 && choices[0].value == "1" &&
                     choices[0].label == "person:7"));
            REQUIRE((identity.rows.size() == 3 && identity.rows[1] == "padding" && identity.rows[2] == "padding"));
        }
        lease.publish(identity);
        const auto select = lease.rowId(0);
        REQUIRE(lease.accepts(identity, 0, select));
        // MobileApply additionally requires a present enabled choice; padding is not
        // exported at all, so neither the rejected passenger nor a blank row can act.
        for (const auto &choice : choices)
            REQUIRE(choice.index == 0);
        eligible->SetAttribute("character", "8");
        REQUIRE(collectChooser());
        REQUIRE(!lease.accepts(identity, 0, select));
        eligible->SetAttribute("character", "7");
        REQUIRE(collectChooser("lscchar_all", 24));
        REQUIRE(!lease.accepts(identity, 0, select));
        REQUIRE(collectChooser("lscchar_all", 23, "Other"));
        REQUIRE(!lease.accepts(identity, 0, select));
        REQUIRE(collectChooser());
        REQUIRE(lease.accepts(identity, 0, select));
        // Missing owner on a nonempty row or an explicit malformed ID remains fatal.
        for (const char *bad : {"-1", "+7", "7x", "4294967296", "", "99"})
        {
            padding->SetAttribute("character", bad);
            REQUIRE(!collectChooser());
        }
        padding->DeleteAttributeClassX(padding->GetAttributeClass("character"));
        padding->SetAttribute("img1", "face");
        REQUIRE(!collectChooser());
        padding->DeleteAttributeClassX(padding->GetAttributeClass("img1"));
        padding->SetValue("unexpected");
        REQUIRE(!collectChooser());
        padding->SetValue(nullptr);
        // A middle hole with a later owner is NOT the stock compacted list shape.
        auto *later = chooser.CreateAttribute("pic3", nullptr);
        later->SetAttribute("character", "8");
        REQUIRE(!collectChooser());
        chooser.DeleteAttributeClassX(later);
        chooser.SetAttribute("ListSize", "4");
        REQUIRE(!collectChooser());
        chooser.SetAttribute("ListSize", "3");
        for (const char *screen : {"ship", "shipyard", "transfer_main"})
            REQUIRE(!collectChooser(screen));
        // No eligible passengers: an empty pic1 + absent pic2 must export no choice.
        eligible->DeleteAttributeClassX(eligible->GetAttributeClass("character"));
        chooser.DeleteAttributeClassX(padding);
        chooser.SetAttribute("ListSize", "2");
        REQUIRE(!collectChooser());
        REQUIRE(choices.empty());
    }
    attrs.SetAttribute("ListSize", "3");
    attrs.SetAttribute("current", "0");
    auto *one = attrs.CreateAttribute("pic1", nullptr);
    one->SetAttribute("companionIndex", "7");
    one->SetAttribute("character", "7");
    auto *two = attrs.CreateAttribute("pic2", nullptr);
    two->SetAttribute("companionIndex", "8");
    two->SetAttribute("character", "8");
    auto label = [](uint32_t owner, bool ship) {
        return owner == 7 || owner == 8 ? std::string(ship ? "vessel:" : "person:") + std::to_string(owner)
                                        : std::string();
    };
    OfficerListIdentity state;
    std::vector<MobileWidget> rows;
    OfficerSelection capability;
    auto collect = [&](const char *screen, const char *node, bool vertical = false, uint64_t epoch = 17,
                       const char *profile = "QA") {
        return OwnerRows(screen, node, vertical, &attrs, 3, epoch, profile, true, 10, label, "Empty ship slot", state,
                         rows);
    };
    for (const char *screen : {"store", "storage", "contraband", "cannons", "hirecrew", "repair"})
    {
        REQUIRE(collect(screen, "SHIPS_SCROLL"));
        REQUIRE((rows.size() == 2 && rows[0].label == "vessel:7" && rows[1].index == 1));
        REQUIRE(state.rows.size() == 3);
        capability.publish(state);
        const auto action = capability.rowId(1);
        REQUIRE(capability.accepts(state, 1, action));
        two->SetAttribute("companionIndex", "7");
        REQUIRE(collect(screen, "SHIPS_SCROLL"));
        REQUIRE(!capability.accepts(state, 1, action));
        two->SetAttribute("companionIndex", "8");
        REQUIRE(collect(screen, "SHIPS_SCROLL", false, 18));
        REQUIRE(!capability.accepts(state, 1, action));
        REQUIRE(collect(screen, "SHIPS_SCROLL", false, 17, "Other profile"));
        REQUIRE(!capability.accepts(state, 1, action));
    }
    for (const char *screen : {"itemstrade", "itemsbox"})
    {
        REQUIRE(collect(screen, "CHARACTERS_SCROLL"));
        REQUIRE(rows[0].label == "person:7");
    }
    for (const char *screen : {"ship", "shipyard"})
        REQUIRE(collect(screen, "SHIPS_SCROLL", true));
    for (const char *screen : {"ship", "shipyard", "character", "lscchar_all", "transfer_main"})
        REQUIRE(collect(screen, "PASSENGERSLIST"));
    REQUIRE(!collect("save_load", "SHIPS_SCROLL"));
    REQUIRE(!collect("items", "CHARACTERS_SCROLL"));
    REQUIRE(!collect("store", "CHARACTERS_SCROLL"));
    REQUIRE(!collect("store", "SHIPS_SCROLL", true));
    REQUIRE(!collect("store", "SHIPS_SCROLL", false, 0));
    REQUIRE(!collect("store", "SHIPS_SCROLL", false, 17, ""));
    for (const char *bad : {"-1", "+1", "7x", "4294967296", ""})
    {
        one->SetAttribute("companionIndex", bad);
        REQUIRE(!collect("store", "SHIPS_SCROLL"));
    }
    one->SetAttribute("companionIndex", "7");
    one->SetAttribute("character", "-1");
    REQUIRE(collect("shipyard", "SHIPS_SCROLL", true));
    REQUIRE(rows[0].label == "Empty ship slot");
    REQUIRE(!collect("ship", "SHIPS_SCROLL", true));
    REQUIRE(!collect("itemstrade", "CHARACTERS_SCROLL"));
    one->SetAttribute("character", "7");
    attrs.SetAttribute("ListSize", "4");
    REQUIRE(!collect("store", "SHIPS_SCROLL"));
    attrs.SetAttribute("ListSize", "3");
    attrs.SetAttribute("current", "-1");
    REQUIRE(!collect("store", "SHIPS_SCROLL"));
    attrs.SetAttribute("current", "3");
    REQUIRE(!collect("store", "SHIPS_SCROLL"));
    attrs.SetAttribute("current", "0");
    REQUIRE(collect("hirecrew", "SHIPS_SCROLL"));
    capability.publish(state);
    const auto guard = capability.rowId(0);
    for (const char *node : {"QTY_OK_BUTTON", "QTY_CANCEL_BUTTON", "QTY_EDIT", "QTY_ADD_BUTTON", "QTY_REMOVE_BUTTON",
                             "QTY_ADD_ALL_BUTTON", "QTY_REMOVE_ALL_BUTTON"})
        REQUIRE(HireCrewQuantityGuard("hirecrew", node, capability, state, rows, guard));
    auto accepts = [&](const std::string &row) {
        return HireCrewQuantityGuard("hirecrew", "QTY_OK_BUTTON", capability, state, rows, row);
    };
    REQUIRE(!HireCrewQuantityGuard("store", "QTY_OK_BUTTON", capability, state, rows, guard));
    REQUIRE(!HireCrewQuantityGuard("hirecrew", "QTY_UNKNOWN", capability, state, rows, guard));
    for (const std::string row : std::vector<std::string>{"", capability.rowId(1), guard + "|press"})
        REQUIRE(!accepts(row));
    attrs.SetAttribute("current", "1");
    REQUIRE(collect("hirecrew", "SHIPS_SCROLL"));
    REQUIRE(!accepts(guard));
    attrs.SetAttribute("current", "2");
    REQUIRE(collect("hirecrew", "SHIPS_SCROLL"));
    REQUIRE(!accepts(guard)); // padding
    attrs.SetAttribute("current", "0");
    REQUIRE(collect("hirecrew", "SHIPS_SCROLL", false, 17, "Other"));
    REQUIRE(!accepts(guard));
    REQUIRE(collect("hirecrew", "SHIPS_SCROLL", false, 18));
    REQUIRE(!accepts(guard));
    one->SetAttribute("companionIndex", "8");
    REQUIRE(collect("hirecrew", "SHIPS_SCROLL"));
    REQUIRE(!accepts(guard));
    one->SetAttribute("companionIndex", "7");
    REQUIRE(collect("hirecrew", "SHIPS_SCROLL"));
    REQUIRE(accepts(guard));
    rows[0].enabled = false;
    REQUIRE(!accepts(guard));
    rows[0].enabled = true;
    rows[1].value = "1";
    REQUIRE(!accepts(guard));
    rows.clear();
    REQUIRE(!accepts(guard));
    ATTRIBUTES active(codec);
    active.SetAttribute("index", "7");
    REQUIRE(HireCrewBoundOwner(&attrs, &active));
    attrs.SetAttribute("current", "1");
    REQUIRE(!HireCrewBoundOwner(&attrs, &active)); // ProcessFrame has not run
    active.SetAttribute("index", "8");
    REQUIRE(HireCrewBoundOwner(&attrs, &active));
    attrs.SetAttribute("current", "2");
    REQUIRE(!HireCrewBoundOwner(&attrs, &active));
    attrs.SetAttribute("current", "1");
    active.SetAttribute("index", "bad");
    REQUIRE(!HireCrewBoundOwner(&attrs, &active));
    REQUIRE(!HireCrewBoundOwner(nullptr, &active));
    REQUIRE(!HireCrewBoundOwner(&attrs, nullptr));
    std::cout << "PASS real owner selectors, padding, known empty shipyard slots, exact fields, owner/profile/epoch "
                 "and malformed guards; hirecrew quantity owner/profile/epoch/selection validation\n";
}

TEST_CASE("an owner list whose current is no row is told, not left without rows")
{
    using namespace storm::mobile;
    Codec codec;
    ATTRIBUTES list(codec);
    list.SetAttribute("ListSize", "2");
    list.CreateAttribute("pic1", nullptr)->SetAttribute("companionIndex", "7");
    list.CreateAttribute("pic2", nullptr)->SetAttribute("companionIndex", "8");
    CHECK(OwnerCurrentDefect("store", "SHIPS_SCROLL", false, &list, 5) == "its current is '', not a row");
    list.SetAttribute("current", "2");
    CHECK(OwnerCurrentDefect("store", "SHIPS_SCROLL", false, &list, 5) == "its current 2 is past its 2 rows");
    list.SetAttribute("current", "1");
    CHECK(OwnerCurrentDefect("store", "SHIPS_SCROLL", false, &list, 5).empty());
    CHECK(OwnerCurrentDefect("store", "OTHER_SCROLL", false, &list, 5).empty()); // not an owner list
}
