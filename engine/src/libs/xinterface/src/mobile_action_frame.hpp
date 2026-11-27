#pragma once

#include <string>

namespace storm::mobile
{
// What became of one widget action of the touch UI (XINTERFACE::MobileApplyAction). The UI gets
// the answer: applied once the action's script events have run (the model that follows shows its
// result), refused with the reason at once. A deferred action is answered when it is retried.
struct ActionOutcome
{
    enum State
    {
        Applied,
        Refused,
        Deferred,
    } state = Applied;
    std::string reason;
};

// The touch UI's widget actions of one interface frame (XINTERFACE::MobileApplyActions) run in the order the player
// made them, each action's script events before the next action (Execute would run them newest first). Widget actions
// are silent: the press click came with "uisound:press" when the gesture started.
class UiAction
{
  public:
    // Scope of one widget action of the touch UI (XINTERFACE::MobileApplyActions).
    class Scope
    {
      public:
        explicit Scope(UiAction &action) : action_(action)
        {
            action_.active_ = true;
        }
        ~Scope()
        {
            action_.active_ = false;
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

      private:
        UiAction &action_;
    };
    // True while a widget action of the touch UI runs: CINODE::DoAction does not click.
    bool Active() const
    {
        return active_;
    }

  private:
    bool active_ = false;
};
inline UiAction uiAction;

// Rounds of the game's answer in one frame: the trade adapter's longest chain is two (a confirmed deal opened again:
// the row's selection answered, then its window; a quick take: the window, then its whole stack confirmed). A screen
// that sends again on every build would never end.
inline constexpr int kSettleRounds = 4;

// The touch UI's answer in the frame of its action: while `pending()` (actions queued, the interface the same),
// `round()` applies them with their script events and hands the screen the model after them, which may send the next
// step. False when actions are still pending after kSettleRounds rounds.
template <class Pending, class Round> bool SettleActions(Pending &&pending, Round &&round)
{
    for (int done = 0; pending(); ++done)
    {
        if (done == kSettleRounds)
            return false;
        round();
    }
    return true;
}
} // namespace storm::mobile
