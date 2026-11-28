// The wave field's inverse 2D transform (libavutil's av_tx, linked for video on every platform). README.md.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

struct AVTXContext;

namespace storm::sea::ocean
{
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
    std::vector<std::complex<float>> scratch_;
};
} // namespace storm::sea::ocean
