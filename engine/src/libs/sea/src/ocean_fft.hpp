// The wave field's inverse 2D transform (libavutil's av_tx, linked for video on every platform). README.md.
#pragma once

#include <complex>
#include <cstddef>
#include <new>
#include <vector>

struct AVTXContext;

namespace storm::sea::ocean
{
// av_tx's vector transforms load and store aligned to the widest vector the CPU has (av_cpu_max_align: 64 bytes with
// AVX-512); a buffer of plain new (16 bytes) crashes them whenever it lands off that boundary.
inline constexpr std::size_t kFftAlignment = 64;

template <class T> struct FftAllocator
{
    using value_type = T;
    FftAllocator() = default;
    template <class U> FftAllocator(const FftAllocator<U> &)
    {
    }
    T *allocate(std::size_t n)
    {
        return static_cast<T *>(::operator new(n * sizeof(T), std::align_val_t{kFftAlignment}));
    }
    void deallocate(T *p, std::size_t)
    {
        ::operator delete(p, std::align_val_t{kFftAlignment});
    }
    friend bool operator==(const FftAllocator &, const FftAllocator &)
    {
        return true;
    }
};

// The values a transform runs on: aligned for av_tx (InverseFft2d::Run refuses others).
using FftBuffer = std::vector<std::complex<float>, FftAllocator<std::complex<float>>>;

// An inverse complex FFT of size x size values, in place: data[y * size + x] holds the coefficient of frequency
// (x, y) (index i stands for i, or i - size from size / 2 on) and becomes the sum over all frequencies of
// coefficient * e^(+2 pi i (fx x + fy y) / size). Not normalised. One per thread: the context has its own scratch.
class InverseFft2d
{
  public:
    explicit InverseFft2d(int size);
    ~InverseFft2d();
    InverseFft2d(const InverseFft2d &) = delete;
    InverseFft2d &operator=(const InverseFft2d &) = delete;

    void Run(std::complex<float> *data);
    int Size() const
    {
        return size_;
    }

  private:
    int size_;
    AVTXContext *context_ = nullptr;
    void (*transform_)(AVTXContext *, void *, void *, std::ptrdiff_t) = nullptr;
    FftBuffer scratch_;
};
} // namespace storm::sea::ocean
