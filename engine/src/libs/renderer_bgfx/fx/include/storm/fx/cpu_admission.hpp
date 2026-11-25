#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace storm::fx
{
// Shared CPU work admission, asked again for every piece of work: a relief network's tile gets as many threads as the
// phase allows now, a bake piece one. FIFO, cancellation polled while parked.
//
// Two budgets. Under a place's opaque loading picture the work takes every core: nothing is played and the load
// ends sooner. While the game is played it takes the gameplay limit: a quarter of the cores, at most two (six of
// eight cores at full load next to the game brought a phone to thermal status SEVERE in a minute and a
// half and its GPU to throttle), and less when the renderer says the device is hot (set_gameplay_limit: one thread,
// or none, effects/thermal.hpp). A limit of none admits nothing new until it rises or a place loads. Work admitted
// under the loading picture keeps its threads only until its piece ends: the next piece gets the gameplay share.
class CpuAdmission
{
  public:
    explicit CpuAdmission(unsigned cores = std::thread::hardware_concurrency())
        : cores_(std::max(1u, cores)), limit_(normal_gameplay_threads())
    {
    }
    struct Stats
    {
        unsigned phase_cap, helper_ceiling, reserved, reserved_peak;
    };
    Stats stats() const
    {
        std::lock_guard lock(mutex_);
        return {opaque_ ? cores_ : limit_, cores_, used_, peak_};
    }
    unsigned maximum_threads() const
    {
        return cores_;
    }
    size_t queued() const
    {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }
    unsigned normal_gameplay_threads() const
    {
        return std::clamp(cores_ / 4, 1u, 2u);
    }
    unsigned gameplay_threads() const
    {
        std::lock_guard lock(mutex_);
        return limit_;
    }
    // The threads the background work may take while the game is played; 0: none (see above). True when it changed.
    bool set_gameplay_limit(unsigned threads)
    {
        {
            std::lock_guard lock(mutex_);
            threads = std::min(threads, cores_);
            if (limit_ == threads)
                return false;
            limit_ = threads;
        }
        wake_.notify_all();
        return true;
    }
    // The share now: every core under the loading picture, else the gameplay limit, never none (work that may not
    // start waits in acquire).
    unsigned threads() const
    {
        std::lock_guard lock(mutex_);
        return opaque_ ? cores_ : std::max(1u, limit_);
    }
    // Generation changes invalidate queued pre-admission selection. No caller
    // waits here; only the relief worker waits for the first real stock Present.
    void begin(uint64_t generation, bool active)
    {
        {
            std::lock_guard lock(mutex_);
            generation_ = generation;
            phase_ = active;
            opaque_ = false;
            ++revision_;
        }
        wake_.notify_all();
    }
    void suspend()
    {
        {
            std::lock_guard lock(mutex_);
            opaque_ = false;
        }
        wake_.notify_all();
    }
    void suspend(uint64_t generation)
    {
        {
            std::lock_guard lock(mutex_);
            if (generation_ == generation)
                opaque_ = false;
        }
        wake_.notify_all();
    }
    void end(uint64_t generation)
    {
        {
            std::lock_guard lock(mutex_);
            if (generation_ != generation)
                return;
            phase_ = opaque_ = false;
            ++revision_;
        }
        wake_.notify_all();
    }
    void opaque(uint64_t generation, bool valid)
    {
        {
            std::lock_guard lock(mutex_);
            if (phase_ && generation_ == generation)
                opaque_ = valid;
        }
        wake_.notify_all();
    }
    bool wait_for_opaque(const std::function<bool()> &cancelled)
    {
        std::unique_lock lock(mutex_);
        const auto revision = revision_;
        while (phase_ && !opaque_)
        {
            if ((cancelled && cancelled()) || revision_ != revision)
                return false;
            wake_.wait_for(lock, std::chrono::milliseconds(10));
        }
        return revision_ == revision && !(cancelled && cancelled());
    }
    // A permit for *permit threads (at least one, at most `wanted`), held while they work: the phase's share when
    // this caller is first in line. Null when cancelled or `wanted` is 0.
    using Permit = std::shared_ptr<const unsigned>;
    Permit acquire(unsigned wanted, const std::function<bool()> &cancelled)
    {
        if (!wanted)
            return {};
        std::unique_lock lock(mutex_);
        const auto id = ++serial_;
        queue_.push_back(id);
        for (;;)
        {
            if (cancelled && cancelled())
            {
                queue_.erase(std::find(queue_.begin(), queue_.end(), id));
                lock.unlock();
                wake_.notify_all();
                return {};
            }
            const unsigned cap = opaque_ ? cores_ : limit_;
            const unsigned count = std::min(wanted, cap);
            if (queue_.front() == id && count && used_ + count <= cap)
            {
                queue_.pop_front();
                used_ += count;
                peak_ = std::max(peak_, used_);
                lock.unlock();
                wake_.notify_all();
                return Permit(new unsigned(count), [this](const unsigned *p) {
                    {
                        std::lock_guard lock(mutex_);
                        used_ -= *p;
                    }
                    delete p;
                    wake_.notify_all();
                });
            }
            wake_.wait_for(lock, std::chrono::milliseconds(10));
        }
    }

  private:
    const unsigned cores_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    unsigned used_ = 0, peak_ = 0, limit_;
    uint64_t serial_ = 0;
    std::deque<uint64_t> queue_;
    bool opaque_ = false, phase_ = false;
    uint64_t generation_ = 0, revision_ = 0;
};
// Defined once in bakes.cpp, linked into the engine, never a header-local singleton.
CpuAdmission &SharedCpuAdmission();
} // namespace storm::fx
