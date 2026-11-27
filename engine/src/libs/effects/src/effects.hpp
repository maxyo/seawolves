// "Effects": the entity of a game's Effekseer effects (README.md).
#pragma once

#include "effect_table.hpp"

#include "entity.h"
#include "storm/render/effekseer.hpp"

class VRenderer;

namespace storm::effects
{
// The game's effects.json, read on first use; errors are logged once.
const EffectTable &Table();

// Plays an Effekseer entry of the table at `position`, turned by `angles`, at the table's speed; kNoInstance when it
// cannot be played (logged).
render::effekseer::InstanceId PlayEntry(const EffectEntry &entry, const render::effekseer::Vec3 &position,
                                        const render::effekseer::Vec3 &angles = {},
                                        const render::effekseer::Vec3 &scale = {1, 1, 1}, int32_t start_frame = 0);
} // namespace storm::effects

class Effects final : public Entity
{
  public:
    ~Effects() override;
    bool Init() override;
    void ProcessStage(Stage stage, uint32_t delta) override;
    uint64_t ProcessMessage(MESSAGE &message) override;

  private:
    VRenderer *renderer_ = nullptr;
};
