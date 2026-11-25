#pragma once
// A ship entity for checks of code that reads ships through `core`: its character and ship attributes, id, sail state
// and death come from the check; anything else fails the check (std::logic_error).
#include "ship_base.h"

#include <stdexcept>

// Entity ids are EntityManager's to give (entity.h makes it a friend): the checks give their entities theirs.
class EntityManager
{
  public:
    static void SetId(Entity &entity, entid_t id)
    {
        entity.data_.id = id;
    }
};

namespace storm::test
{
class FakeShip final : public SHIP_BASE
{
    [[noreturn]] static void unmodelled(const char *what)
    {
        throw std::logic_error(std::string("FakeShip: the check does not model ") + what);
    }

  public:
    float sailState = 1;
    bool dead = false;

    FakeShip(entid_t id, ATTRIBUTES *character, ATTRIBUTES *ship)
    {
        EntityManager::SetId(*this, id);
        SetACharacter(character);
        pAShip = ship;
    }
    float GetSailState() override
    {
        return sailState;
    }
    bool isDead() override
    {
        return dead;
    }

    // Not modelled.
    void ProcessStage(Stage, uint32_t) override
    {
        unmodelled("ProcessStage");
    }
    float Trace(const glm::vec3 &, const glm::vec3 &) override
    {
        unmodelled("Trace");
    }
    bool Clip(const PLANE *, int32_t, const glm::vec3 &, float, ADD_POLYGON_FUNC) override
    {
        unmodelled("Clip");
    }
    const char *GetCollideMaterialName() override
    {
        unmodelled("GetCollideMaterialName");
    }
    bool GetCollideTriangle(TRIANGLE &) override
    {
        unmodelled("GetCollideTriangle");
    }
    float Cannon_Trace(int32_t, const glm::vec3 &, const glm::vec3 &) override
    {
        unmodelled("Cannon_Trace");
    }
    bool Mount(ATTRIBUTES *) override
    {
        unmodelled("Mount");
    }
    void SetPos(const glm::vec3 &) override
    {
        unmodelled("SetPos");
    }
    glm::vec3 GetPos() const override
    {
        unmodelled("GetPos");
    }
    glm::vec3 GetAng() const override
    {
        unmodelled("GetAng");
    }
    glm::mat4 *GetMatrix() override
    {
        unmodelled("GetMatrix");
    }
    glm::vec3 GetBoxsize() const override
    {
        unmodelled("GetBoxsize");
    }
    MODEL *GetModel() const override
    {
        unmodelled("GetModel");
    }
    entid_t GetModelEID() const override
    {
        unmodelled("GetModelEID");
    }
    void Save(CSaveLoad *) override
    {
        unmodelled("Save");
    }
    void Load(CSaveLoad *) override
    {
        unmodelled("Load");
    }
    BOOL BuildContour(glm::vec3 *, int32_t &) override
    {
        unmodelled("BuildContour");
    }
    BOOL TouchMove(uint32_t, TOUCH_PARAMS *, TOUCH_PARAMS *) override
    {
        unmodelled("TouchMove");
    }
    int32_t AddStrength(STRENGTH *) override
    {
        unmodelled("AddStrength");
    }
    bool DelStrength(int32_t) override
    {
        unmodelled("DelStrength");
    }
    float GetMaxSpeedZ() override
    {
        unmodelled("GetMaxSpeedZ");
    }
    float GetMaxSpeedY() override
    {
        unmodelled("GetMaxSpeedY");
    }
    float GetWindAgainst() override
    {
        unmodelled("GetWindAgainst");
    }
    void SetSailState(float) override
    {
        unmodelled("SetSailState");
    }
    void SetRotate(float) override
    {
        unmodelled("SetRotate");
    }
    float GetRotate() override
    {
        unmodelled("GetRotate");
    }
    float GetBrakingDistance(float *) override
    {
        unmodelled("GetBrakingDistance");
    }
    float GetRotationAngle(float *) override
    {
        unmodelled("GetRotationAngle");
    }
    float GetCurrentSpeed() override
    {
        unmodelled("GetCurrentSpeed");
    }
    void SetDead() override
    {
        unmodelled("SetDead");
    }
    void SetLightAndFog(bool) override
    {
        unmodelled("SetLightAndFog");
    }
    void RestoreLightAndFog() override
    {
        unmodelled("RestoreLightAndFog");
    }
    void SetLights() override
    {
        unmodelled("SetLights");
    }
    void UnSetLights() override
    {
        unmodelled("UnSetLights");
    }
    void Fire(const glm::vec3 &) override
    {
        unmodelled("Fire");
    }
    void SetFixedSpeed(bool, float) override
    {
        unmodelled("SetFixedSpeed");
    }
};
} // namespace storm::test
