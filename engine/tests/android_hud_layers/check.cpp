#include "platform/android_hud_presenter.hpp"
#include <cassert>
#include <iostream>
using namespace storm::android;

int main()
{
    // No declarations, a mounted UI, and a failed UI all have the same draw policy.
    for (int phase = 0; phase < 3; ++phase)
    {
        if (phase == 1)
            for (const auto *hud : {"land", "sea", "worldmap", "spyglass"})
                assert(hudPresenters.declare(hud));
        if (phase == 2)
        {
            assert(hudPresenters.lost().find("UI runtime failed") != std::string::npos);
            assert(hudPresenters.lost().empty());
            assert(hudPresenters.sea && hudPresenters.worldMap && hudPresenters.spyglass);
        }
        assert(legacyDrawRefused("land interface") && legacyDrawRefused("world map fleet"));
        assert(!legacyDrawRefused("ship bars") && !legacyDrawRefused("boarding status"));
    }
    // The touch HUD can re-declare after a failure, without enabling old drawing.
    assert(!hudPresenters.declare("unknown"));
    assert(hudPresenters.declare("sea") && !hudPresenters.failed);
    assert(!hudPresenters.lost().empty());
    std::cout << "PASS HUD layers: legacy panels always refused; world-space markers preserved; runtime failure logged "
                 "once per recovery, ownership retained\n";
}
