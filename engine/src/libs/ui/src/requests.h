// The one ledger of what the UI asked the game: every request it sends (a widget action of the open interface)
// gets a number, the engine answers each one, applied or refused, and an applied request is shown by the first
// model that arrives after its answer. Busy marks, amount drafts and predictions end on it, not on a clock.
#pragma once

#include <cstdint>
#include <iterator>
#include <map>

namespace storm::ui
{

class RequestLedger
{
  public:
    enum class Status
    {
        None,    // nothing was sent
        Waiting, // sent, not answered yet
        Applied, // applied; the model that shows it has not arrived
        Shown,   // applied, and a model arrived after the answer
        Refused,
    };

    // Numbers a request that is being sent.
    uint64_t Open()
    {
        requests_[++last_] = Status::Waiting;
        return last_;
    }
    // The number the next request gets.
    uint64_t Next() const
    {
        return last_ + 1;
    }
    // The engine's answer; false when `request` is not waiting (one of an earlier opening, or answered).
    bool Answer(uint64_t request, bool applied)
    {
        const auto found = requests_.find(request);
        if (found == requests_.end() || found->second != Status::Waiting)
            return false;
        found->second = applied ? Status::Applied : Status::Refused;
        return true;
    }
    // A model arrived: every request applied before it is shown.
    void Arrived()
    {
        for (auto &[request, status] : requests_)
            if (status == Status::Applied)
                status = Status::Shown;
        // Finished requests are kept for From() of the drafts and predictions still open; 256 is far more than
        // one screen sends between two models.
        for (auto it = requests_.begin(); requests_.size() > 256 && it != requests_.end();)
            it = it->second == Status::Waiting || it->second == Status::Applied ? std::next(it) : requests_.erase(it);
    }
    Status Of(uint64_t request) const
    {
        const auto found = requests_.find(request);
        return found == requests_.end() ? Status::None : found->second;
    }
    // The requests numbered from `floor` on, together: Waiting while one waits, else Refused when one was refused,
    // else Applied while one is not shown, else Shown; None when nothing was sent from there.
    Status From(uint64_t floor) const
    {
        bool waiting = false, refused = false, applied = false, any = false;
        for (auto it = requests_.lower_bound(floor); it != requests_.end(); ++it)
        {
            waiting = waiting || it->second == Status::Waiting;
            refused = refused || it->second == Status::Refused;
            applied = applied || it->second == Status::Applied;
            any = true;
        }
        return waiting   ? Status::Waiting
               : refused ? Status::Refused
               : applied ? Status::Applied
               : any     ? Status::Shown
                         : Status::None;
    }
    // Something sent is not answered or not shown yet.
    bool Busy() const
    {
        for (const auto &[request, status] : requests_)
            if (status == Status::Waiting || status == Status::Applied)
                return true;
        return false;
    }
    // The interface closed or opened again: nothing sent to the old one will be answered or shown.
    void Drop()
    {
        requests_.clear();
    }

  private:
    uint64_t last_ = 0;
    std::map<uint64_t, Status> requests_;
};

} // namespace storm::ui
