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

// The touch UI's widget actions of one interface frame (XINTERFACE::DoControl) run in the order the player made them,
// each action's script events before the next action (Execute would run them newest first). Widget actions are
// silent: the press click came with "uisound:press" when the gesture started.
class UiAction
{
  public:
    // Scope of one widget action of the touch UI (XINTERFACE::DoControl).
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
} // namespace storm::mobile
