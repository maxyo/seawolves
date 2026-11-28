// Character: directional locomotion (locomotion.hpp): the action table the game's scripts set, and the clip the legs'
// state plays.
#include "character.h"

#include "core.h"

#include <cmath>

using namespace storm::location;

void Character::StartMove(MoveDir dir)
{
    if (priorityAction.name)
        return;
    isMove = dir != MoveDir::none;
    isBack = false;
    wantDir = dir;
}

void Character::ReadDirectionalActions(ATTRIBUTES *actions)
{
    auto &t = *directional;
    const auto read = [actions](ActionMove &am, const std::string &attribute) {
        am.SetName(actions->GetAttribute(attribute.c_str()));
    };
    for (size_t g = 0; g < kGaits; g++)
        for (size_t d = 0; d < kMoveDirs; d++)
        {
            const auto gait = static_cast<Gait>(g);
            const auto dir = static_cast<MoveDir>(d + 1);
            for (size_t f = 0; f < 2; f++)
            {
                read(t.move[f][g][d], moveAttribute(f != 0, gait, dir));
                read(t.start[f][g][d], startAttribute(f != 0, gait, dir));
            }
            for (size_t s = 0; s < kStopSteps; s++)
                read(t.stop[g][d][s], stopAttribute(gait, dir, s));
        }
    for (size_t f = 0; f < 2; f++)
        for (size_t d = 0; d < kStairsDirs; d++)
            for (size_t u = 0; u < 2; u++)
                read(t.stairs[f][d][u], stairsAttribute(f != 0, d, u == 0));
}

void Character::UpdateDirectionalData(Animation *a)
{
    auto &t = *directional;
    for (auto &fight : t.move)
        for (auto &gait : fight)
            for (auto &am : gait)
                UpdateActionMoveData(am, a);
    for (auto &fight : t.start)
        for (auto &gait : fight)
            for (auto &am : gait)
                UpdateActionMoveData(am, a);
    for (auto &gait : t.stop)
        for (auto &dir : gait)
            for (auto &am : dir)
                UpdateActionMoveData(am, a);
    for (auto &fight : t.stairs)
        for (auto &dir : fight)
            for (auto &am : dir)
                UpdateActionMoveData(am, a);
}

void Character::StartMove(const MoveIntent &intent)
{
    SetRunMode(intent.run);
    SetSprint(intent.sprint);
    if (intent.dir == MoveDir::none)
        StopMove();
    else
        StartMove(intent.dir);
}

bool Character::SetSprint(bool want)
{
    if (!want || overload)
        sprinting = false;
    else if (!sprinting)
    {
        int32_t possible = 1;
        if (VDATA *res = core.Event("ChrCheckEnergy", "is", GetId(), "sprint"))
            res->Get(possible);
        sprinting = possible != 0;
    }
    return sprinting;
}

void Character::Dodge(storm::location::Dodge dodge)
{
    switch (dodge)
    {
    case storm::location::Dodge::back:
        return Recoil();
    case storm::location::Dodge::right:
        return StrafeRight();
    case storm::location::Dodge::left:
        return StrafeLeft();
    case storm::location::Dodge::none:
        break;
    }
}

bool Character::IsSprint() const
{
    return directional && stride.phase != StridePhase::still && DirectionalGait(stride.dir) == Gait::sprint;
}

Gait Character::DirectionalGait(MoveDir dir) const
{
    return gaitFor(dir, IsRun(), sprinting && !overload);
}

Character::ActionMove *Character::DirectionalClip()
{
    auto &t = *directional;
    const size_t f = isFight ? 1 : 0, d = dirIndex(stride.dir);
    size_t g = static_cast<size_t>(DirectionalGait(stride.dir));
    if (movecs <= CHARACTER_COS_STAIR && stride.phase != StridePhase::stopping)
        return &t.stairs[f][stairsIndex(stride.dir)][isUp ? 0 : 1];
    // The scripts name a forward-diagonal sprint that the animation sets do not have: that diagonal runs.
    if (g == static_cast<size_t>(Gait::sprint) && !t.move[f][g][d].inAnimation)
        g = static_cast<size_t>(Gait::run);
    switch (stride.phase)
    {
    case StridePhase::starting:
        return &t.start[f][g][d];
    case StridePhase::stopping:
        return &t.stop[g][d][stopStepAfter(stride.lastStep)];
    default:
        return &t.move[f][g][d];
    }
}

bool Character::StepStride()
{
    auto &t = *directional;
    const MoveDir wanted = isMove ? wantDir : MoveDir::none;
    const size_t f = isFight ? 1 : 0;
    const size_t g = static_cast<size_t>(DirectionalGait(wanted != MoveDir::none ? wanted : stride.dir));
    // Walking starts and stops at once, and a fight has no stop clips: the games' animation sets have none.
    const bool clips = g != static_cast<size_t>(Gait::walk);
    const bool hasStart = clips && wanted != MoveDir::none && t.start[f][g][dirIndex(wanted)].inAnimation;
    const bool hasStop = clips && !isFight && stride.dir != MoveDir::none &&
                         t.stop[g][dirIndex(stride.dir)][stopStepAfter(stride.lastStep)].inAnimation;
    stride.want(wanted, hasStart, hasStop);
    return stride.phase != StridePhase::still;
}

void Character::PlayDirectionalClip()
{
    curIdleIndex = -1;
    ActionMove *clip = DirectionalClip();
    if (curMove == clip)
        return;
    curMove = clip;
    if (!SetAction(clip->name, clip->tblend, std::fabs(clip->speed), clip->turnspd))
        core.Trace("Character animation: not set move action: \"%s\"", clip->name ? clip->name : "");
}

float Character::DirectionalPace() const
{
    if (!curMove || curMove->easing == Easing::none)
        return 1.0f;
    MODEL *m = Model();
    Animation *a = m ? m->GetAnimation() : nullptr;
    return a ? ease(curMove->easing, a->Player(0).GetPosition()) : 1.0f;
}
