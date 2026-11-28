// The hero's locomotion with the touch stick and the cameras it steers relative to: the parts of Character::Move,
// Player, LocationCamera and CameraFollow the movement check runs (tests/android_movement) with its own owner types.
// Each struct is a friend of the class whose members it reads.
#pragma once

#include "core.h"
#include "locomotion.hpp"
#include "storm_glm.h"

#include <algorithm>
#include <cmath>
#include <optional>

#ifdef STORM_TOUCH_UI
#include <platform/android_camera.hpp>
#include <platform/android_movement.hpp>
#endif

namespace storm::location
{

// Character::Move: the touch vector's share of a frame's locomotion.
struct CharacterMotion
{
    // What the touch stick does to this frame's movement: stopped by it, moved by it (at `speed`), and the animation's
    // pace for that speed. Without touch input (and off Android): nothing, the native pace.
    struct TouchStep
    {
        bool stop = false, motion = false;
        float speed = 0.0f, pace = 1.0f;
    };

#ifdef STORM_TOUCH_UI
    template <class C> static TouchStep touchStep(C &self)
    {
        // Only ordinary locomotion may consume the touch vector. Priority, recoil,
        // attack, jump, swim and scripted movement retain all existing physics.
        const bool touchStop = storm::android::ordinaryMovementAllowed(
            self.androidMotionStop && !self.isMove, true, self.lockMove, self.priorityAction.name != nullptr,
            self.isJump, self.isSwim, self.fgtCurType != C::fgt_none, self.fgtSetType != C::fgt_none, true);
        const bool touchMotion =
            touchStop ||
            storm::android::ordinaryMovementAllowed(
                self.androidMotion, self.isMove, self.lockMove, self.priorityAction.name != nullptr, self.isJump,
                self.isSwim, self.fgtCurType != C::fgt_none, self.fgtSetType != C::fgt_none, self.curMove != nullptr);
        self.androidMotionApplied = touchMotion;
        if (touchMotion && !touchStop && self.UsesTouchCamera() && !self.isTurnLock)
        {
            // The native pose and collision heading must agree with the voluntary
            // travel vector. Keep all angles, including diagonals, continuous; a
            // camera looking at the actor's face cannot make a forward gait run
            // backwards. Special/locked motion never reaches this branch.
            self.ay = self.nay = atan2f(self.androidMotionDirection.x, self.androidMotionDirection.z);
            self.turnDir = 0.0f;
        }
        float touchSpeed = touchStop ? 0.0f : self.androidMotionSpeed;
        if (touchMotion && self.curMove && self.androidMotionWalkSpeed > .001f &&
            (self.curMove == &self.stsUp || self.curMove == &self.stsDown || self.curMove == &self.stsUpBack ||
             self.curMove == &self.stsDownBack))
            touchSpeed *= fabsf(self.curMove->speed) / self.androidMotionWalkSpeed;
        const float animationPace = touchMotion && !touchStop && self.curMove && fabsf(self.curMove->speed) > .001f
                                        ? touchSpeed / fabsf(self.curMove->speed)
                                        : 1.0f;
        return {touchStop, touchMotion, touchSpeed, animationPace};
    }
#endif

    // The frame's travel: the gait along the heading with its strafe, or the touch vector; then the impulse.
    template <class C> static void displace(C &self, float dltTime, float k, [[maybe_unused]] const TouchStep &touch)
    {
        const float spd = dltTime * self.speed * k;
        glm::vec3 moveVec(sinf(self.ay) * spd, 0.0f, cosf(self.ay) * spd);
        if (self.directional)
        {
            // The clip's speed along the stride's direction turned by the heading; the speed's sign means nothing.
            const auto travel = worldTravel(self.stride.dir, self.ay);
            const float step = dltTime * std::fabs(self.speed) * k * self.DirectionalPace();
            self.curPos += glm::vec3(travel.x, 0.0f, travel.z) * step;
        }
#ifdef STORM_TOUCH_UI
        else if (touch.motion)
            self.curPos += self.androidMotionDirection * (dltTime * touch.speed * k);
#endif
        else
        {
            self.curPos += moveVec;
            if (spd < 0.0f)
                moveVec = -moveVec;
            self.curPos += glm::vec3(moveVec.z, 0.0f, -moveVec.x) * (self.strafeMove * 0.8f);
        }
        self.curPos += self.impulse * dltTime;
    }

    // The strafe in place, which the touch vector replaces.
    template <class C> static void strafeInPlace(C &self, float dltTime, [[maybe_unused]] const TouchStep &touch)
    {
#ifdef STORM_TOUCH_UI
        if (!touch.motion)
#endif
            self.curPos += glm::vec3(cosf(self.ay), 0.0f, -sinf(self.ay)) * (self.strafeVel * dltTime * 0.9f);
    }
};

// Player: the touch stick and the native controls deciding the hero's gait.
struct PlayerMotion
{
    template <class P> static void StrafeWhenMove(P &self, float dltTime)
    {
        self.strafeMove = 0.0f;

        // If you need to turn off strafe - uncomment return
        // stopstrafe

        if (!self.isSpecialMode)
        {
            CONTROL_STATE cs;
            if (core.Controls->GetControlState("ChrStrafeLeft", cs))
            {
                if (cs.lValue != 0)
                {
                    self.strafeMove += -1.0f;
                }
            }
            if (core.Controls->GetControlState("ChrStrafeRight", cs))
            {
                if (cs.lValue != 0)
                {
                    self.strafeMove += 1.0f;
                }
            }
        }
    }

    template <class P> static void StrafeWhenStop(P &self, float dltTime)
    {
        self.StrafeWhenMove(dltTime);
#ifdef STORM_TOUCH_UI
        // The native poll above owns physical A/D, including configurations whose
        // stopped strafe is subsequently cleared or converted into a combat dodge.
        if (self.strafeMove != 0.0f)
            self.androidMotionStop = false;
#endif
        if (self.IsFight())
        {
            if (self.strafeMove > 0.5f)
            {
                self.StrafeRight();
            }
            else if (self.strafeMove < -0.5f)
            {
                self.StrafeLeft();
            }
            self.strafeMove = 0.0f;
        }
        else
        {
            self.strafeMove = 0.0f;
        }
    }

    // The native controls' gait: forward, back (a recoil in a fight) or a stop.
    template <class P> static void controlsGait(P &self, float dltTime)
    {
        self.SetRunMode(self.IsRunMode(dltTime));
        if (self.GoForward(dltTime))
        {
#ifdef STORM_TOUCH_UI
            self.androidMotionStop = false;
            self.androidCameraRelative = false;
#endif
            self.StartMove(false);
            self.StrafeWhenMove(dltTime);
        }
        else if (self.GoBack(dltTime))
        {
#ifdef STORM_TOUCH_UI
            self.androidMotionStop = false;
            self.androidCameraRelative = false;
#endif
            if (!self.IsFight())
            {
                self.StartMove(true);
                self.StrafeWhenMove(dltTime);
            }
            else
            {
                self.Recoil();
            }
        }
        else
        {
#ifdef STORM_TOUCH_UI
            if (self.androidMovementHeld)
                self.StopAndroidMovement();
            else
#endif
                self.StopMove();
            self.StrafeWhenStop(dltTime);
        }
    }

    // Directional locomotion: the held keys' direction relative to the heading, which the mouse turns; a run unless
    // the walk toggle flips the character's "alwaysrun".
    template <class P> static void directionalGait(P &self, float dltTime)
    {
        CONTROL_STATE cs;
        const auto held = [&cs](const char *control) {
            return core.Controls->GetControlState(control, cs) && cs.lValue != 0;
        };
        const bool forward = held("ChrForward") || held("ChrForward2"), back = held("ChrBackward"),
                   right = held("ChrStrafeRight"), left = held("ChrStrafeLeft"), sprintKey = held("ChrSprint");
        self.sprintKey.update(sprintKey, self.IsFight(), self.inputLagShift(), dltTime);
        switch (dodgeFor(self.IsFight(), sprintKey, forward, back, right, left))
        {
        case Dodge::back:
            return self.Recoil();
        case Dodge::right:
            return self.StrafeRight();
        case Dodge::left:
            return self.StrafeLeft();
        case Dodge::none:
            break;
        }
        if (core.Controls->GetControlState("ChrSwitchWalk", cs) && cs.state == CST_ACTIVATED)
            self.walkToggled = !self.walkToggled;
        self.SetSprint(self.sprintKey.held);
        directionalIntent(self, {directionFromKeys(forward, back, right, left), self.alwaysRun() != self.walkToggled});
    }

    template <class P> static void directionalIntent(P &self, const MoveIntent &intent)
    {
        self.SetRunMode(intent.run);
        if (intent.dir == MoveDir::none)
            self.StopMove();
        else
            self.StartMove(intent.dir);
    }

#ifdef STORM_TOUCH_UI
    // The held touch stick under directional locomotion: the stick's direction as keys, relative to the camera, which
    // the hero turns to while he walks.
    template <class P> static void directionalStick(P &self, const storm::android::MovementAxes &axes)
    {
        auto intent = intentFromStick(axes.x, axes.y);
        intent.run = intent.run && axes.runAllowed;
        self.androidMotionStop = intent.dir == MoveDir::none;
        if (intent.dir != MoveDir::none && self.androidCameraRelative && self.locCam && self.locCam->IsMovementCamera())
        {
            const glm::vec3 view = self.locCam->MovementViewDirection();
            self.Turn(view.x, view.z);
        }
        directionalIntent(self, intent);
    }

    template <class P> static void StopAndroidMovement(P &self)
    {
        self.StopMove();
        self.strafeMove = 0.0f;
        self.androidMotionStop = true;
        self.androidRunTier = false;
    }

    // The stick's sample of this frame (none: not held): who owns the hero, whether he stops, and whether the camera
    // is the frame of the stick.
    template <class P> static void takeAxes(P &self, const std::optional<storm::android::MovementAxes> &androidAxes)
    {
        if (self.task.task != P::npct_none || self.command.cmd != P::aicmd_none ||
            self.GetId() != core.GetEntityId("Player"))
        {
            self.androidMotionStop = false;
            self.androidRunTier = false;
            if (androidAxes)
                storm::android::movementInput.resetActivity();
        }
        else if ((self.androidMovementHeld && !androidAxes) || (androidAxes && androidAxes->magnitude == 0.0f))
        {
            // StopMove only requests idle; bspeed still holds the full native gait
            // during blending. Do not let that residual speed replace a slow touch
            // vector after release, zero input, expiry or hard cancellation.
            self.StopAndroidMovement();
        }
        const auto cameraGeneration = storm::android::movementInput.generation();
        if (cameraGeneration != self.androidCameraGeneration || self.task.task != P::npct_none ||
            self.command.cmd != P::aicmd_none || self.GetId() != core.GetEntityId("Player") ||
            (self.locCam && !self.locCam->IsMovementCamera()))
            self.androidCameraRelative = false;
        self.androidCameraGeneration = cameraGeneration;
        if (androidAxes && self.task.task == P::npct_none && self.command.cmd == P::aicmd_none &&
            (!self.locCam || self.locCam->IsMovementCamera()))
            self.androidCameraRelative = true;
        self.androidMovementHeld = androidAxes.has_value();
    }

    // The held stick's gait: a stop, the swim it steers, a combat backstep or side dodge, or a walk or run along it.
    template <class P> static void stickGait(P &self, const storm::android::MovementAxes &axes)
    {
        const auto gait = storm::android::touchGait(axes, self.isSwim, self.IsFight());
        if (gait == storm::android::TouchGait::stop)
        {
            self.StopAndroidMovement();
        }
        else if (gait == storm::android::TouchGait::swim)
        {
            // In water the native swim moves the hero along his heading at its own
            // pace (no run, no strafe, the waves on top): the stick steers it, relative
            // to the camera as on land.
            const bool cameraRelative = self.androidCameraRelative && self.locCam && self.locCam->IsMovementCamera();
            const glm::vec3 forward =
                cameraRelative ? self.locCam->MovementViewDirection() : glm::vec3(sinf(self.ay), 0.0f, cosf(self.ay));
            const auto direction = storm::android::movementDirection(axes, forward.x, forward.z, self.ay);
            self.androidRunTier = false;
            self.SetRunMode(false);
            self.StartMove(false);
            self.androidMotionStop = false;
            self.strafeMove = 0.0f;
            self.Turn(direction.x, direction.z);
        }
        else if (gait == storm::android::TouchGait::combatBack)
        {
            // Backstep and side dodges are native combat actions, not
            // ordinary locomotion whose impulses may be rescaled.
            self.StopAndroidMovement();
            if (axes.magnitude > .22f)
                self.Recoil();
        }
        else if (gait == storm::android::TouchGait::combatSide)
        {
            self.StopAndroidMovement();
            if (axes.magnitude > .22f)
            {
                if (axes.x < 0)
                    self.StrafeLeft();
                else
                    self.StrafeRight();
            }
        }
        else
        {
            // Explore input turns the actor towards its travel vector;
            // moving towards the camera is a forward gait after a turn,
            // never a backwards gait selected from the screen-space Y.
            const bool cameraRelative = self.UsesTouchCamera();
            const bool back = !cameraRelative && axes.y < 0;
            const float walking =
                self.IsFight() ? self.fightwalk.speed : (back ? self.backwalk.speed : self.walk.speed);
            const float running = self.IsFight() ? self.fightrun.speed : (back ? self.backrun.speed : self.run.speed);
            const auto pace = storm::android::movementPace(axes.magnitude, walking, running,
                                                           axes.runAllowed && !self.isRunDisable, self.androidRunTier);
            self.androidRunTier = pace.run;
            self.SetRunMode(pace.run);
            self.StartMove(back);
            self.androidMotionStop = false;
            self.strafeMove = cameraRelative ? 0.0f : axes.x;
            // Combat stays relative to the fighter, like native
            // recoil/strafe. Camera inertia cannot reverse its forward gait.
            const glm::vec3 forward = cameraRelative && self.locCam ? self.locCam->MovementViewDirection()
                                                                    : glm::vec3(sinf(self.ay), 0.0f, cosf(self.ay));
            const auto direction = storm::android::movementDirection(axes, forward.x, forward.z, self.ay);
            self.androidMotionDirection = glm::vec3(direction.x, 0.0f, direction.z);
            self.androidMotionSpeed = pace.speed;
            self.androidMotionWalkSpeed = std::fabs(walking);
            self.androidMotion = true;
        }
    }
#endif
};

// LocationCamera and its follow camera (CameraFollow).
struct CameraMotion
{
    // Execution of a camera observing from a point
    template <class L> static void ExecuteTopos(L &self)
    {
        // Where are we looking at
        self.lookTo = self.pos + glm::vec3(0.0f, self.lheight, 0.0f);
        // Where are we looking from
        self.camPos = self.fromLook;
    }

    template <class F> static void MoveCamera(F &self, float dltTime)
    {
        if (dltTime <= 0.0f)
            return;
#ifdef STORM_TOUCH_UI
        // Mobile camera mode, out of a fight (the native camera above swings behind the hero's heading at
        // once and turns him with the drag, which fights a stick that steers relative to the camera). The
        // drag turns the camera by all of it (the hero only turns with the stick, Player::Rotate); while
        // the hero moves without a drag the camera is pulled round behind him after a short delay
        // (storm::android::AutoFollow). In tight spaces it comes closer, swings sideways, lowers or rises
        // a little to keep the hero framed (storm::android::tightView), never to a top-down view.
        if (self.lc->character->MobileCameraOrbit() && !self.lc->isSpecialMode)
        {
            self.MoveMobile(dltTime);
            return;
        }
#endif
        // Where are we looking
        auto likeCamay = self.lc->chay;
        if (self.lc->isSpecialMode)
        {
            likeCamay = self.camay + self.lc->dAy;
        }
        self.lookTo = self.lc->pos;
        self.lookTo.y += self.lc->lheight;
        // decide what to do
        const auto pi = 3.14159265359f;
        auto chay = likeCamay - static_cast<int32_t>(likeCamay / pi) * 2.0f * pi;
        const auto cay = self.camay - static_cast<int32_t>(self.camay / pi) * 2.0f * pi;
        if (cay - chay > pi)
            chay += 2 * pi;
        if (cay - chay < -pi)
            chay -= 2 * pi;
        const auto dAng = chay - cay;
        // Calculate the current radius
        self.kRadInert = fabsf(dAng) / 0.05f;
        if (self.kRadInert > 1.0f)
            self.kRadInert = 1.0f;
        self.kRadInert = powf(self.kRadInert, 5.0f);
        if (fabsf(dAng) > 0.001f)
        {
            // The camera rotates around the character
            auto k = dltTime * self.lc->rotInertia;
            if (k > 1.0f)
                k = 1.0f;
            self.camay = cay + dAng * k;
            self.lc->character->LockRotate(true);
        }
        self.ChangeRadius(dltTime, self.FindRadius(self.camay, self.lc->ax));
        self.CalcPosition(self.camay, self.camradius, 0.0f, self.camPos);
    }

#ifdef STORM_TOUCH_UI
    // Use the actual camera, not a renderer view temporarily changed by UI/shadow draws.
    template <class L> static bool IsMovementCamera(const L &self)
    {
        return (self.wmode == L::cwm_topos || (self.wmode == L::cwm_follow && !self.isLookMode)) &&
               !self.isLockFPMode && !self.isSleep && !self.m_bTrackMode;
    }

    template <class L> static void UpdateMovementHeading(L &self)
    {
        const auto view = self.lookTo - self.camPos;
        const float horizontal = std::hypot(view.x, view.z);
        // At the vertical pole screen-forward has no horizontal direction. Keep
        // the last camera heading, never the actor heading (which the stick turns).
        if (std::isfinite(horizontal) && std::isfinite(view.y) && horizontal > .001f &&
            horizontal > .001f * std::fabs(view.y))
            self.movementForward = glm::vec3(view.x / horizontal, 0.0f, view.z / horizontal);
    }

    template <class F> static void MoveMobile(F &self, float dltTime)
    {
        using namespace storm::android;
        const bool settle = self.settle_; // the first frame in a location: the view is taken at once
        self.settle_ = false;
        // The finger always wins: the drag turns the view by all of it, stops a swing under way and holds
        // the automatic turns off until the hero has stood and walks again (AutoFollow).
        const bool dragged = self.lc->dragAy != 0.0f || self.lc->dragAx != 0.0f;
        self.camay += self.lc->dragAy;
        if (dragged)
            self.swing.reset();
        const glm::vec3 walked = self.lc->pos - self.heroPos;
        self.heroPos = self.lc->pos;
        self.camay +=
            self.autoFollow.step(dltTime, dragged, std::sqrt(walked.x * walked.x + walked.z * walked.z) / dltTime,
                                 self.lc->chay - self.camay);
        self.lookTo = self.lc->pos;
        self.lookTo.y += self.lc->lheight;
        // The room wanted: kTightClose, or most of a radius zoomed in shorter.
        const float want = std::min(kTightClose, self.lc->radius * 0.9f);
        const float room = self.FindRadius(self.camay, self.lc->ax);
        const bool tight = self.tightRaise != 0.0f || self.swing.value != 0.0f;
        self.tightAge -= dltTime;
        if (!tight && room >= want)
            ; // closer behind him is enough (ChangeRadius pulls in)
        else if (tight && self.swing.value == 0.0f && room >= want + kTightGain)
            self.tightRaise = 0.0f; // room again at the player's own view (with a margin: no back and forth)
        else if (settle || (self.tightAge <= 0.0f &&
                            self.FindRadius(self.camay + self.swing.value, self.lc->ax + self.tightRaise) <
                                (tight ? want * kTightKeep : want)))
        {
            // Sideways only while the player has not dragged since the hero last stood (a swing made since does
            // not forbid another: its view has closed). Judged from where a swing under way goes, so a choice
            // never takes it back (otherwise the view stopped half way, raised, inside the hero's hiding
            // distance: only the floor seen).
            const float heading = self.camay + self.swing.value;
            const TightView view =
                tightView(self.lc->ax, want, settle || (!dragged && !self.autoFollow.dragged()),
                          [&](float yaw, float pitch) { return self.FindRadius(heading + yaw, pitch); });
            if (view.yaw != 0.0f)
            {
                self.swing.reset(self.swing.value + view.yaw);
                self.autoFollow.hold();
            }
            self.tightRaise = view.pitch - self.lc->ax;
            self.tightAge =
                view.room >= want ? 0.0f : 0.5f; // none good: not every frame (the probes, a restless choice)
        }
        float turn = 0.0f;
        if (settle)
        {
            turn = self.swing.value;
            self.swing.reset();
            self.raise.reset(self.tightRaise);
        }
        else if (self.swing.value != 0.0f)
        {
            const float before = self.swing.value;
            self.swing.step(0.0f, dltTime, kSwingTime);
            turn = std::clamp(before - self.swing.value, -kSwingMax * dltTime, kSwingMax * dltTime);
            self.swing.value = before - turn;
            if (std::fabs(self.swing.value) < 1e-3f)
            {
                turn = before;
                self.swing.reset();
            }
        }
        self.camay += turn;
        self.raise.step(self.tightRaise, dltTime, kRaiseTime);
        if (std::fabs(self.raise.value - self.tightRaise) < 1e-3f && std::fabs(self.raise.velocity) < 1e-2f)
            self.raise.reset(self.tightRaise);
        const float radius = self.raise.value != 0.0f || turn != 0.0f
                                 ? self.FindRadius(self.camay, self.lc->ax + self.raise.value)
                                 : room;
        if (settle)
            self.camradius = std::min(radius, self.lc->radius);
        self.ChangeRadius(dltTime, radius);
        self.CalcPosition(self.camay, self.camradius, -self.raise.value, self.camPos);
    }
#endif
};

} // namespace storm::location
