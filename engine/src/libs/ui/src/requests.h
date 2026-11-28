// The one ledger of what the UI asked the game: every request it sends (a widget action of the open interface, a
// command of the HUD) gets a number, the engine answers each one, applied or refused. An applied widget action is
// shown by the first interface model that arrives after its answer; an applied HUD command by the state of its
// channel at the revision the answer names. Busy marks, amount drafts, predictions and the HUD's tapped choices end
// on it, not on a clock.
#pragma once

#include <cstdint>
#include <iterator>
#include <map>
#include <string>

namespace storm::ui
{

class RequestLedger
{
  public:
    enum class Status
    {
        None,    // nothing was sent
        Waiting, // sent, not answered yet
        Applied, // applied; the model or state that shows it has not arrived
        Shown,   // applied, and what shows it arrived
        Refused,
    };

    // Numbers a request that is being sent: a widget action of the interface (no channel), or a HUD command whose
    // effect the HUD state channel `channel` shows.
    uint64_t Open(std::string channel = {})
    {
        requests_[++last_] = {Status::Waiting, std::move(channel), 0};
        return last_;
    }
    // The number the next request gets.
    uint64_t Next() const
    {
        return last_ + 1;
    }
    // The engine's answer; `revision` is, for a HUD command, the revision of its channel that shows it. False when
    // `request` is not waiting (one of an earlier opening, or answered).
    bool Answer(uint64_t request, bool applied, uint64_t revision = 0)
    {
        const auto found = requests_.find(request);
        if (found == requests_.end() || found->second.status != Status::Waiting)
            return false;
        Entry &entry = found->second;
        entry.revision = revision;
        if (!applied)
            entry.status = Status::Refused;
        else if (const auto arrived = arrived_.find(entry.channel);
                 !entry.channel.empty() && arrived != arrived_.end() && arrived->second >= revision)
            entry.status = Status::Shown; // the state that shows it came before the answer
        else
            entry.status = Status::Applied;
        return true;
    }
    // An interface model arrived: every widget action applied before it is shown.
    void Arrived()
    {
        for (auto &[request, entry] : requests_)
            if (entry.status == Status::Applied && entry.channel.empty())
                entry.status = Status::Shown;
        Prune();
    }
    // The state of a HUD channel at `revision` arrived: the commands it shows are shown.
    void Arrived(const std::string &channel, uint64_t revision)
    {
        arrived_[channel] = revision;
        for (auto &[request, entry] : requests_)
            if (entry.status == Status::Applied && entry.channel == channel && entry.revision <= revision)
                entry.status = Status::Shown;
        Prune();
    }
    Status Of(uint64_t request) const
    {
        const auto found = requests_.find(request);
        return found == requests_.end() ? Status::None : found->second.status;
    }
    // The widget actions numbered from `floor` on, together: Waiting while one waits, else Refused when one was
    // refused, else Applied while one is not shown, else Shown; None when nothing was sent from there.
    Status From(uint64_t floor) const
    {
        bool waiting = false, refused = false, applied = false, any = false;
        for (auto it = requests_.lower_bound(floor); it != requests_.end(); ++it)
        {
            if (!it->second.channel.empty())
                continue;
            waiting = waiting || it->second.status == Status::Waiting;
            refused = refused || it->second.status == Status::Refused;
            applied = applied || it->second.status == Status::Applied;
            any = true;
        }
        return waiting   ? Status::Waiting
               : refused ? Status::Refused
               : applied ? Status::Applied
               : any     ? Status::Shown
                         : Status::None;
    }
    // A widget action sent is not answered or not shown yet.
    bool Busy() const
    {
        for (const auto &[request, entry] : requests_)
            if (entry.channel.empty() && (entry.status == Status::Waiting || entry.status == Status::Applied))
                return true;
        return false;
    }
    // The interface closed or opened again: nothing sent to the old one will be answered or shown. The HUD's
    // commands are not the interface's: they stay.
    void Drop()
    {
        std::erase_if(requests_, [](const auto &request) { return request.second.channel.empty(); });
    }

  private:
    struct Entry
    {
        Status status = Status::Waiting;
        std::string channel; // "" = the interface model
        uint64_t revision = 0;
    };
    // Finished requests are kept for From() of the drafts and predictions still open and for the HUD's choices;
    // 256 is far more than the UI sends between two models.
    void Prune()
    {
        for (auto it = requests_.begin(); requests_.size() > 256 && it != requests_.end();)
            it = it->second.status == Status::Waiting || it->second.status == Status::Applied ? std::next(it)
                                                                                              : requests_.erase(it);
    }

    uint64_t last_ = 0;
    std::map<uint64_t, Entry> requests_;
    std::map<std::string, uint64_t> arrived_; // the newest revision of each HUD channel
};

} // namespace storm::ui
