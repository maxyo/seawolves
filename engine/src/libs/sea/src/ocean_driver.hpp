// The sea's wave field over time: the field read this frame and the one the worker makes for the next. README.md.
#pragma once

#include "ocean_field.hpp"
#include "ocean_worker.hpp"

#include <exception>
#include <memory>
#include <optional>

namespace storm::sea::ocean
{
class Driver
{
  public:
    // A weather change eases over this long: the old spectrum fades into the new one.
    static constexpr double kEaseSeconds = 8.0;

    // The worker's job uses the fields and transforms below: a sea deleted while it runs (a scene change) waits for it.
    ~Driver()
    {
        worker_.Wait();
    }

    // Once per sea frame, game thread; never waits for the worker. Advances the sea's clock by `dt` seconds. When the
    // worker has finished its field, that field becomes the one drawn and read (Current), the weather's `keys` are
    // taken (a change eases in unless `snap`: then, and at a new layout `cascades` x `size`, the field is made here at
    // once) and the worker starts on the next field at the clock's time; returns true. While the worker is still
    // busy the frame keeps the field it drew last and returns false: a late field shows its waves a frame older,
    // and WaveXZ still reads exactly what is drawn.
    [[nodiscard]] bool Frame(int cascades, int size, const Sea2Keys &keys, double dt, bool snap);

    // The field drawn and queried this frame (empty layout before the first Frame).
    const Field &Current() const
    {
        return *fields_[current_];
    }
    // The current field's slope texture of `cascade` with all its levels (PackSlopeMoments), made by the worker.
    const std::vector<uint64_t> &SlopeMoments(size_t cascade) const
    {
        return moments_[current_][cascade];
    }
    bool Ready() const
    {
        return fields_[current_] != nullptr;
    }
    // Milliseconds the worker spent on the field read this frame (its transforms and any spectrum it built).
    double WorkerMs() const
    {
        return worker_ms_;
    }
    // Milliseconds the game thread waited this frame for the worker: only at a new layout or a snapped weather.
    double WaitMs() const
    {
        return wait_ms_;
    }

  private:
    void Layout(int cascades, int size, const Sea2Keys &keys);
    void Run(std::optional<SeaState> build);

    Worker worker_;
    std::unique_ptr<Field> fields_[2];
    std::vector<std::vector<uint64_t>> moments_[2];         // per field, per cascade
    std::vector<std::unique_ptr<InverseFft2d>> transforms_; // per cascade: one per helper thread
    int current_ = 0;
    int cascades_ = 0, size_ = 0;
    Sea2Keys keys_{};
    double clock_ = 0.0, ease_ = 1.0;
    double late_dt_ = 0.0; // the clock's advance over frames the worker was late for (the easing catches it up)
    // Written by the worker's job, read after Wait.
    std::optional<SeaState> built_state_;
    std::vector<CascadeSpectrum> built_;
    std::exception_ptr failure_;
    double job_ms_ = 0.0, worker_ms_ = 0.0, wait_ms_ = 0.0;
};
} // namespace storm::sea::ocean
