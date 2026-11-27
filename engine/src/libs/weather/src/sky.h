#pragma once

#include "entity.h"
#include "storm/environment/legacy.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

// The sky: drawn by the renderer (storm/render/effects.hpp DrawSky) in the layers it is placed in and in the sea's
// reflection. The scripts of «Каждому своё» create it and give it their weather's sky folders (Dir.d<i>, one per
// part of the day, and Dir = the hour): what the folders show is the clouds' cover (environment::LegacySkyFolder).
// WEATHER creates one when the environment comes from the game's data.
class SKY : public Entity
{
  public:
    bool Init() override
    {
        return true;
    }
    uint32_t AttributeChanged(ATTRIBUTES *attribute) override;
    uint64_t ProcessMessage(MESSAGE &message) override;
    void ProcessStage(Stage stage, uint32_t delta) override;

    // The folders' cover at `hour`, between the two folders around it; nullopt without folders.
    std::optional<storm::environment::LegacySky> Cover(float hour) const;

  private:
    std::vector<storm::environment::LegacySky> folders_; // the day in equal parts, from midnight
    std::set<std::string> reported_;
};
