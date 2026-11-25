#pragma once
// The game's dialogue on its way to the UI host (EngineUi::DialogueState): every state the engine sends reaches the
// host, an equal one again too (a retried answer refused the same way must be shown refused again), and a host that
// starts later gets the newest. A template over the host so that the check needs no whole storm::ui::Host.
#include "storm_ui/dialogue_state.h"

#include <optional>
#include <utility>

namespace storm::android::ui
{

class DialogueRelay
{
  public:
    // `host`: none before the first host has started; the state is kept for it then.
    template <class Host> void Send(Host *host, storm::ui::Dialogue dialogue)
    {
        if (host)
            host->SetDialogue(dialogue);
        newest_ = std::move(dialogue);
    }
    template <class Host> void Replay(Host &host) const
    {
        if (newest_)
            host.SetDialogue(*newest_);
    }

  private:
    std::optional<storm::ui::Dialogue> newest_;
};

} // namespace storm::android::ui
