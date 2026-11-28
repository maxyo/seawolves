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

    // Once per sea frame, game thread. Waits for the field the worker made during the last frame and makes it the one
    // read (Current), takes `cascades` x `size` and the weather's `keys` (a change eases in unless `snap`: then, and
    // at a new layout, the field is made here at once), advances the sea's clock by `dt` seconds and starts the
    // worker on the next field.
    void Frame(int cascades, int size, const Sea2Keys &keys, double dt, bool snap);

    // The field drawn and queried this frame (empty layout before the first Frame).
    const Field &Current() const
    {
        return *fields_[current_];
    }
    // The current field's slopes of `cascade` as the pixel shader's texture holds them: four half floats a sample
    // (hx, hz, dxx, dzz), packed by the worker.
    const std::vector<uint64_t> &SlopeHalves(size_t cascade) const
    {
        return halves_[current_][cascade];
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
    // Milliseconds the game thread waited this frame for the worker to finish (0 when it was done in time).
    double WaitMs() const
    {
        return wait_ms_;
    }

  private:
    void Layout(int cascades, int size, const Sea2Keys &keys);
    void Run(std::optional<SeaState> build);
    static void Pack(const Field &field, size_t cascade, std::vector<uint64_t> &out);

    Worker worker_;
    std::unique_ptr<Field> fields_[2];
    std::vector<std::vector<uint64_t>> halves_[2];          // per field, per cascade
    std::vector<std::unique_ptr<InverseFft2d>> transforms_; // per cascade: one per helper thread
    int current_ = 0;
    int cascades_ = 0, size_ = 0;
    Sea2Keys keys_{};
    double clock_ = 0.0, ease_ = 1.0;
    // Written by the worker's job, read after Wait.
    std::optional<SeaState> built_state_;
    std::vector<CascadeSpectrum> built_;
    std::exception_ptr failure_;
    double job_ms_ = 0.0, worker_ms_ = 0.0, wait_ms_ = 0.0;
};
} // namespace storm::sea::ocean
