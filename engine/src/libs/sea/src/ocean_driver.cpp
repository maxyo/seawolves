#include "ocean_driver.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace storm::sea::ocean
{
namespace
{
bool Same(const Sea2Keys &a, const Sea2Keys &b)
{
    return std::memcmp(&a, &b, sizeof(Sea2Keys)) == 0;
}
} // namespace

void Driver::Layout(int cascades, int size, const Sea2Keys &keys)
{
    worker_.Wait();
    const std::vector<Cascade> layout = CascadeLayout(cascades, size);
    const SeaState state = MapSea2(keys);
    const std::vector<CascadeSpectrum> spectra = BuildSpectrum(state, layout);
    for (auto &field : fields_)
    {
        field = std::make_unique<Field>(layout);
        field->SetSpectra(spectra, state.choppiness, state.mean_level);
    }
    transforms_.clear();
    for (const Cascade &cascade : layout)
        transforms_.push_back(std::make_unique<InverseFft2d>(cascade.size));
    cascades_ = cascades;
    size_ = size;
    keys_ = keys;
    ease_ = 1.0;
    built_state_.reset();
    for (auto &moments : moments_)
        moments.assign(layout.size(), {});
    // The first frame reads a field made now.
    fields_[current_]->Update(clock_);
    for (size_t c = 0; c < layout.size(); c++)
        PackSlopeMoments(fields_[current_]->Slopes(c), layout[c].size, moments_[current_][c]);
}

void Driver::Run(std::optional<SeaState> build)
{
    Field &next = *fields_[1 - current_];
    std::vector<std::vector<uint64_t>> &moments = moments_[1 - current_];
    const double time = clock_;
    worker_.Start([this, &next, &moments, time, build = std::move(build)] {
        const auto start = std::chrono::steady_clock::now();
        try
        {
            worker_.Parallel(transforms_.size(), [&](size_t c) {
                next.UpdateCascade(c, time, *transforms_[c]);
                PackSlopeMoments(next.Slopes(c), next.Layout()[c].size, moments[c]);
            });
            if (build)
            {
                built_ = BuildSpectrum(*build, next.Layout());
                built_state_ = build;
            }
        }
        catch (...)
        {
            failure_ = std::current_exception();
        }
        job_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    });
}

bool Driver::Frame(int cascades, int size, const Sea2Keys &keys, double dt, bool snap)
{
    if (!fields_[0] || cascades != cascades_ || size != size_ || (snap && !Same(keys, keys_)))
    {
        clock_ += dt;
        const auto wait = std::chrono::steady_clock::now();
        Layout(cascades, size, keys);
        Run(std::nullopt);
        worker_ms_ = 0.0;
        wait_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wait).count();
        late_dt_ = 0.0;
        return true;
    }
    wait_ms_ = 0.0;
    if (worker_.Busy())
    {
        clock_ += dt;
        late_dt_ += dt;
        return false;
    }
    if (failure_)
        std::rethrow_exception(std::exchange(failure_, nullptr));
    worker_ms_ = job_ms_;
    current_ = 1 - current_;
    if (built_state_)
    {
        // Both fields ease alike: the one read now and the one the worker makes next.
        for (auto &field : fields_)
            field->EaseTo(built_, built_state_->choppiness, built_state_->mean_level);
        built_state_.reset();
        ease_ = 0.0;
    }
    std::optional<SeaState> build;
    if (!Same(keys, keys_))
    {
        keys_ = keys;
        build = MapSea2(keys);
    }
    const double step = dt + std::exchange(late_dt_, 0.0);
    if (ease_ < 1.0)
    {
        ease_ = std::min(1.0, ease_ + step / kEaseSeconds);
        for (auto &field : fields_)
            field->Ease(static_cast<float>(ease_));
    }
    clock_ += dt;
    Run(std::move(build));
    return true;
}
} // namespace storm::sea::ocean
