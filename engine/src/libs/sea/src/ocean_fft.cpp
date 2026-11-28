#include "ocean_fft.hpp"

extern "C"
{
#include <libavutil/tx.h>
}

#include <stdexcept>
#include <string>

namespace storm::sea::ocean
{
InverseFft2d::InverseFft2d(int size) : size_(size), scratch_(static_cast<size_t>(size) * size)
{
    static_assert(sizeof(AVComplexFloat) == sizeof(std::complex<float>));
    float scale = 1.0f;
    av_tx_fn transform = nullptr;
    if (const int error = av_tx_init(&context_, &transform, AV_TX_FLOAT_FFT, 1, size, &scale, 0); error < 0)
        throw std::runtime_error("sea: no FFT of size " + std::to_string(size) + " (av_tx_init " +
                                 std::to_string(error) + ")");
    transform_ = transform;
}

InverseFft2d::~InverseFft2d()
{
    av_tx_uninit(&context_);
}

void InverseFft2d::Run(std::complex<float> *data)
{
    // Rows into the scratch, transposed back, twice: the second pass transforms the columns.
    const size_t n = static_cast<size_t>(size_);
    for (int pass = 0; pass < 2; pass++)
    {
        for (size_t y = 0; y < n; y++)
            transform_(context_, &scratch_[y * n], &data[y * n], sizeof(std::complex<float>));
        for (size_t y = 0; y < n; y++)
            for (size_t x = 0; x < n; x++)
                data[x * n + y] = scratch_[y * n + x];
    }
}
} // namespace storm::sea::ocean
