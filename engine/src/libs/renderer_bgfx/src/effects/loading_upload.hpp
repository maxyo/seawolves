#pragma once

#include <cstddef>
#include <cstdint>

namespace storm::effects
{
// The extra x2 uploads of a frame under the presented loading cover (SceneUploadScope): only the entries of this
// load (`generation`) that the residency plan selected.
struct LoadingUploadScope
{
    uint64_t generation = 0;
    bool opaque_preparing = false;
    bool active() const
    {
        return generation != 0 && opaque_preparing;
    }
    bool accepts(uint64_t entry_generation, bool selected) const
    {
        return active() && selected && entry_generation == generation;
    }
};

class UploadBatch
{
  public:
    static constexpr unsigned kGameplayUploads = 2, kLoadingUploads = 16;
    static constexpr uint64_t kLoadingBytes = 16u << 20;
    static constexpr double kLoadingMilliseconds = 4.0;

    explicit UploadBatch(bool loading) : loading_(loading)
    {
    }
    bool allows(uint64_t bytes, double elapsed_ms) const
    {
        if (!loading_)
            return uploads_ < kGameplayUploads;
        if (uploads_ >= kLoadingUploads)
            return false;
        // The first upload of a frame always goes, however large: an oversized texture still arrives.
        if (uploads_ == 0)
            return true;
        return elapsed_ms < kLoadingMilliseconds && bytes_ <= kLoadingBytes && bytes <= kLoadingBytes - bytes_;
    }
    void submitted(uint64_t bytes)
    {
        ++uploads_;
        bytes_ += bytes;
    }
    unsigned uploads() const
    {
        return uploads_;
    }
    uint64_t bytes() const
    {
        return bytes_;
    }

  private:
    bool loading_;
    unsigned uploads_ = 0;
    uint64_t bytes_ = 0;
};
} // namespace storm::effects
