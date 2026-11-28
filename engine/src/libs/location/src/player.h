//    Spirenkov Maxim aka Sp-Max Shaman, 2001
//
//    Player

#pragma once

#include "np_character.h"

class LocationCamera;

namespace storm::location
{
struct PlayerMotion;
}

class Player : public NPCharacter
{
    friend storm::location::PlayerMotion;

  public:
    Player();
    ~Player() override;

    bool PostInit() override;

    void Reset() override;

    // Move the character to the desired position
    void Move(float dltTime) override;
    // A blow that reached the hero (or his block): «Вибрация» (platform/android_haptics.hpp).
    void HitChild(bool isInBlock) override;
    void Update(float dltTime) override;

    // Save parameters
    void SetSaveData(ATTRIBUTES *sdata) override;
    // Restore parameters
    void GetSaveData(ATTRIBUTES *sdata) override;

    bool IsPlayer() override
    {
        return true;
    }

    void Rotate(float dltTime);
    bool GoForward(float dltTime);
    bool GoBack(float dltTime) const;
    bool IsRunMode(float dltTime);
    void StrafeWhenMove(float dltTime);
    void StrafeWhenStop(float dltTime);

    bool IsDoBlock();
    bool IsDoParry();
    bool IsDoAttackForce();
    bool IsDoAttackFast();
    bool IsDoAttackRound();
    bool IsDoAttackBreak();
    bool IsDoAttackFeint();
    bool IsFire();
    bool IsChangeFightMode();

    // Find an attacking enemy
    Player *FindAttackCharacter();

  private:
    void FireFromShootgun();
    void ShootParticles(const glm::vec3 &pos, const glm::vec3 &ndir, float size, uint32_t color, int32_t num);

    float GetRotateH();
    // The character's "alwaysrun" (the scripts' option, run by default).
    bool alwaysRun() const;
    // The character's "input_lag_shift": how long a released sprint key still counts, seconds.
    float inputLagShift() const;

  private:
#ifdef STORM_TOUCH_UI
    bool androidMovementHeld = false;
    bool androidRunTier = false;
    uint64_t androidCameraGeneration = 0;
    void StopAndroidMovement();
#endif
    float lastChange;
    bool activatedDialog;
    bool isSpecialMode;
    bool isSetBlock;
    entid_t baterfl;
    bool shootgunMode;
    float kSMReload;
    LocationCamera *locCam;
    bool walkToggled = false; // ChrSwitchWalk flips "alwaysrun"
    storm::location::SprintKey sprintKey;
};
