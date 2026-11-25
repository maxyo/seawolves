#include "platform/android_ship_telemetry.hpp"
#include <limits>

#include <catch2/catch.hpp>

static constexpr uint32_t kCaptions = storm::android::ShipSpeed | storm::android::ShipPowder |
                                      storm::android::ShipDateTime | storm::android::ShipLocation |
                                      storm::android::ShipWind;

TEST_CASE("android_ship_telemetry")
{
    using namespace storm::android;
    const char *defect = nullptr;
    REQUIRE((hudScriptText("Ласточка", defect) == "Ласточка" && !defect));
    REQUIRE((hudScriptText(std::string(300, 'a') + "Я", defect).size() == 302 && !defect)); // no length limit
    for (const auto invalid : {"\xDF", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80"})
    {
        defect = nullptr;
        REQUIRE((hudScriptText(invalid, defect).empty() && defect));
    }
    for (const auto invalid : {"", "unknown", "0junk", "NaN", "Inf", "-1", "1,5"})
        REQUIRE(!shipNumberValue(invalid));
    REQUIRE(shipNumberValue("0") == 0);
    REQUIRE(shipNumberValue(" 1.5e2 ") == 150);
    REQUIRE(!shipMetric(0, 0));
    REQUIRE(!shipMetric(-1, 100));
    REQUIRE(!shipMetric(std::numeric_limits<double>::quiet_NaN(), 100));
    REQUIRE(!shipMetric(50, std::numeric_limits<double>::infinity()));
    REQUIRE(shipMetric(120, 100)->current == 120); // raw values are not fabricated/clamped
    defect = nullptr;
    REQUIRE((hudCaption("  Сан-Хуан  ", defect) == "Сан-Хуан" && !defect));
    REQUIRE((hudCaption(std::string(400, 'a'), defect).size() == 400 && !defect)); // no length limit
    for (const auto invalid : {"\xDF", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "bad\ntext"})
    {
        defect = nullptr;
        REQUIRE((hudCaption(invalid, defect).empty() && defect));
    }
    REQUIRE(hudCaption(shipDateTimeText("Дата: 17 октября 1665 12:34", "Дата:"), defect) == "17 октября 1665 12:34");
    REQUIRE(hudCaption(shipDateTimeText("Date: 17 October 1665 12:34", "Date:"), defect) == "17 October 1665 12:34");
    REQUIRE(hudCaption(shipDateTimeText("Datum: 17 Oktober 1665 12:34", "Datum: "), defect) == "17 Oktober 1665 12:34");
    REQUIRE(shipDateTimeText("Дата: 12:34", "Date:").empty());
    REQUIRE(shipDateTimeText("unknown 12:34", "").empty());
    REQUIRE(shipDateTimeText("12:34 Date:", "Date:").empty());
    REQUIRE(shipDisplayedSpeed(-1.26) == -1.3);
    REQUIRE(shipDisplayedSpeed(-3.15f * 0.4f) == -1.3); // native float scaling, signed arcade speed
    REQUIRE(shipDisplayedSpeed(3.15f * 1.0f) == 3.2);
    REQUIRE(shipDisplayedSpeed(1.24) == 1.2);
    REQUIRE(shipDisplayedSpeed(0) == 0);
    REQUIRE(!shipDisplayedSpeed(std::numeric_limits<double>::infinity()));
    REQUIRE(!shipDisplayedSpeed(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(shipPowderCount("0") == 0);
    REQUIRE(shipPowderCount("125") == 125);
    for (const auto invalid : {"", "-1", "2.5", "NaN", "unknown", "2147483648"})
        REQUIRE(!shipPowderCount(invalid));
    REQUIRE(shipAmmoCount("0") == 0);
    REQUIRE(shipAmmoCount("125") == 125);
    REQUIRE(shipAmmoCount("2147483648") == 2147483648LL);
    for (const auto invalid : {"", "-1", "2.5", "NaN", "Inf", "unknown", "9223372036854775808"})
        REQUIRE(!shipAmmoCount(invalid));
    // Inventory is independent from the selected charge and preserves absent vs zero.
    ShipTelemetryState inventoryState;
    ShipTelemetryFrame inventory;
    inventory.valid = true;
    inventory.ship.entityId = "cargo-owner";
    inventory.ammo = "balls";
    inventory.ammoType = 0;
    inventory.ammoCount = 7;
    inventory.ammoCounts = {0, 25, 36, 47};
    inventory.ship.type = "Lugger";
    inventory.ship.shipClass = 5;
    inventoryState.update(10, inventory);
    const auto inventoryFirst = inventoryState.publication(0);
    REQUIRE(inventoryFirst);
    REQUIRE((inventoryFirst->ammo_count == 7 && inventoryFirst->own.type == "Lugger" &&
             inventoryFirst->own.ship_class == 5));
    REQUIRE((inventoryFirst->ammo_counts[0] == 0 && inventoryFirst->ammo_counts[1] == 25 &&
             inventoryFirst->ammo_counts[2] == 36 && inventoryFirst->ammo_counts[3] == 47));
    { // every bar under its own key: distinct values catch a swap of crew and sails
        ShipTelemetryFrame bars = inventory;
        bars.ship.hull = ShipMetric{410, 1000};
        bars.ship.crew = ShipMetric{33, 120};
        bars.ship.sails = ShipMetric{77, 100};
        ShipTelemetryState barsState;
        barsState.update(12, bars);
        const auto own = barsState.hud().own;
        REQUIRE((own.hull == 0.41 && own.crew == 33.0 / 120.0 && own.sails == 0.77));
    }
    { // the ship's picture: the battle interface's icon atlas, named in the texture namespace
        REQUIRE(shipPictureTexture("battle_interface\\ship_icons2.tga.tx") == "battle_interface/ship_icons2");
        REQUIRE(shipPictureTexture("BATTLE_INTERFACE\\Ship_Icons2.TGA") == "battle_interface/ship_icons2");
        REQUIRE((shipPictureTexture("").empty() && shipPictureTexture("c:\\x.tga").empty() &&
                 shipPictureTexture("..\\x").empty() && shipPictureTexture("\\x.tga").empty()));
        const auto picture = shipPicture("battle_interface\\ship_icons2.tga.tx", {0.5f, 0.125f, 0.5625f, 0.1875f});
        REQUIRE((picture && picture->texture == "battle_interface/ship_icons2"));
        REQUIRE(!shipPicture("battle_interface\\ship_icons2.tga.tx", {0.5f, 0.125f, 0.5f, 0.1875f})); // empty rectangle
        REQUIRE(!shipPicture("battle_interface\\ship_icons2.tga.tx", {0.f, 0.f, 1.5f, 1.f})); // outside the texture
        REQUIRE(!shipPicture("", {0.f, 0.f, 1.f, 1.f}));
        ShipTelemetryFrame pictured = inventory;
        pictured.ship.picture = picture;
        ShipTelemetryState pictureState;
        pictureState.update(13, pictured);
        REQUIRE((pictureState.hud().own.picture.texture == "battle_interface/ship_icons2" &&
                 pictureState.hud().own.picture.uv == std::array<float, 4>{0.5f, 0.125f, 0.5625f, 0.1875f}));
        REQUIRE(inventoryState.hud().own.picture.texture.empty()); // no picture known: none said
    }
    const auto inventoryGeneration = inventoryState.hudRevision();
    inventory.ammoCounts[1] = 24;
    inventoryState.update(10, inventory);
    REQUIRE((inventoryState.hudRevision() == inventoryGeneration && inventoryState.revision() == 10));
    REQUIRE(!inventoryState.publication(99));
    const auto inventoryChanged = inventoryState.publication(100);
    REQUIRE((inventoryChanged && inventoryChanged->ammo_counts[1] == 24 && inventoryChanged->ammo_counts[0] == 0));
    inventory.ammoCounts[2].reset();
    inventoryState.update(10, inventory);
    REQUIRE(inventoryState.hud().ammo_counts[2] == -1.0); // not in the hold: unknown, not empty
    inventory.ammoCounts = {};
    inventoryState.update(10, inventory);
    REQUIRE((inventoryState.hud().ammo_counts[0] == -1.0 && inventoryState.hud().ammo_count == 7));
    inventoryState.update(11, {});
    const auto inventoryCleared = inventoryState.publication(101);
    REQUIRE((inventoryCleared && !inventoryCleared->known && inventoryCleared->ammo_counts[0] == -1.0));

    ShipSailGuard guard{true, true, true, true, false, false, true, 0.5};
    REQUIRE(shipSailControlAllowed(guard));
    for (auto flag : {&ShipSailGuard::sea, &ShipSailGuard::up, &ShipSailGuard::down, &ShipSailGuard::mainCharacter,
                      &ShipSailGuard::canDropSails})
    {
        auto blocked = guard;
        blocked.*flag = false;
        REQUIRE(!shipSailControlAllowed(blocked));
    }
    guard.stopped = true;
    REQUIRE(!shipSailControlAllowed(guard));
    guard.stopped = false;
    guard.dead = true;
    REQUIRE(!shipSailControlAllowed(guard));
    guard.dead = false;
    for (double value : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()})
    {
        guard.state = value;
        REQUIRE(!shipSailControlAllowed(guard));
    }
    guard.state.reset();
    REQUIRE(!shipSailControlAllowed(guard));

    GameplayState gameplay;
    gameplay.update({"sailing", "russian", "Sailing3Pers", "ship-1", {"Ship_SailUp", "Ship_SailDown"}});
    const auto epoch = gameplay.revision();
    ShipTelemetryState state;
    ShipTelemetryFrame frame;
    frame.valid = true;
    frame.ship = {1, "123456789", "Ship", "Lugger", shipMetric(50, 100), shipMetric(30, 40), shipMetric(80, 100)};
    frame.sailState = 0.5;
    frame.canSetSails = true;
    frame.ammo = "balls";
    frame.ammoType = 0;
    frame.ammoCount = 0; // zero inventory is valid, distinct from unknown
    frame.fleet.push_back(
        {2, "123456790", "Friend", "Sloop", shipMetric(10, 100), shipMetric(20, 60), shipMetric(70, 100)});
    state.update(epoch, frame);
    const auto request = [epoch](int value) { return storm::ui::hud::Sails{epoch, value}; };
    {
        // Every legacy HUD layer is refused, whether or not the UI presents its HUD; runtime
        // failure never restores legacy drawing.
        hudPresenters.sea = true;
        REQUIRE((legacyDrawRefused("land interface") && legacyDrawRefused("active perk icons")));
        hudPresenters.sea = false;
        REQUIRE((legacyDrawRefused("land interface") && legacyDrawRefused("active perk icons")));
        REQUIRE((hudPresenters.declare("land") && legacyDrawRefused("timer bar") && !hudPresenters.lost().empty() &&
                 legacyDrawRefused("timer bar")));
    }
    const auto first = state.publication(0);
    REQUIRE((first && first->ammo_count == 0 && first->own.entity == "123456789"));
    REQUIRE(!state.publication(200));
    REQUIRE(state.offerSails(request(0)));
    REQUIRE(state.offerSails(request(2)));
    const auto stableHud = state.hudRevision();
    frame.ship.hull = shipMetric(49, 100);
    state.update(epoch, frame);
    REQUIRE(state.hudRevision() == stableHud);
    REQUIRE(gameplay.revision() == epoch); // HP updates cannot cancel held inputs/action epoch
    REQUIRE(state.takeSails() == 2);       // latest slider value wins; HP changes do not cancel it
    REQUIRE(!state.takeSails());
    REQUIRE(!state.publication(99));
    REQUIRE(state.publication(100));
    frame.ammoCount.reset();
    frame.ship.crew.reset();
    frame.fleet.front().sails.reset();
    state.update(epoch, frame);
    frame.ammoCount = 1;
    state.update(epoch, frame);

    frame.fleet.front().sails = shipMetric(70, 100);
    state.update(epoch, frame);
    frame.fleet.front().entityId = "replacement-entity";
    state.update(epoch, frame);
    REQUIRE(!state.offerSails(request(3)));
    REQUIRE(state.offerSails(request(1)));
    frame.canSetSails = false;
    state.update(epoch, frame);
    REQUIRE(!state.takeSails()); // stopped/quest lock changed after touch
    frame.canSetSails = true;
    state.update(epoch, frame);
    REQUIRE(state.offerSails(request(1)));
    REQUIRE(state.takeSails() == 1);
    REQUIRE(state.offerSails({epoch - 1, 1}) == false); // a lever pulled on the gameplay before does not act
    state.update(epoch + 1, frame);
    REQUIRE(state.publication(42));
    state.update(epoch + 1, {});
    const auto cleared = state.publication(43);
    REQUIRE((cleared && !cleared->known)); // clear bypasses throttle
    // Caption replacements require their own current render ACK. Content changes
    // are throttled without invalidating held controls or the action epoch.
    ShipTelemetryState captions;
    frame.captionsVisible = true;
    frame.shipSpeedKnots = -1.3;
    frame.windSpeed = 5.4;
    frame.powderCount = 0;
    frame.dateTimeText = "17 октября 1665 12:34";
    frame.locationText = "Сан-Хуан";
    captions.update(epoch, frame);
    REQUIRE((captions.frame().availableMask() & kCaptions) == kCaptions);

    const auto allCaptions = captions.publication(0);
    REQUIRE((allCaptions && allCaptions->captions && allCaptions->speed == 1.3 && allCaptions->wind == 5.4 &&
             allCaptions->date_time == "17 октября 1665 12:34" &&
             allCaptions->location == "Сан-Хуан")); // sternway is shown as a speed
    const auto captionRevision = captions.hudRevision();
    frame.shipSpeedKnots = -1.2;
    frame.windSpeed = 5.5;
    frame.powderCount = 1;
    frame.dateTimeText = "17 октября 1665 12:35";
    frame.locationText = "Порт Сан-Хуан";
    captions.update(epoch, frame);
    REQUIRE((captions.hudRevision() == captionRevision && captions.revision() == epoch));

    REQUIRE((!captions.publication(99) && captions.publication(100)));
    frame.captionsVisible = false;
    captions.update(epoch, frame);
    REQUIRE(captions.hudRevision() > captionRevision);
    REQUIRE(captions.publication(1)); // hide bypasses throttle
    frame.captionsVisible = true;
    captions.update(epoch, frame);
    for (const auto bit : {ShipSpeed, ShipWind, ShipPowder, ShipDateTime, ShipLocation})
    {
        auto missing = frame;
        if (bit == ShipSpeed)
            missing.shipSpeedKnots.reset();
        if (bit == ShipWind)
            missing.windSpeed.reset();
        if (bit == ShipPowder)
            missing.powderCount.reset();
        if (bit == ShipDateTime)
            missing.dateTimeText.clear();
        if (bit == ShipLocation)
            missing.locationText.clear();
        captions.update(epoch, missing);
        captions.update(epoch, frame);
    }
    captions.update(epoch, {});
}
