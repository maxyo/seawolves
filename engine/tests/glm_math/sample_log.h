#pragma once

// The results of a harness's randomized cases. Kept, so the optimizer cannot drop the operations the
// sanitizers are meant to see execute; the harness prints their count.
#include <source_location>
#include <vector>

struct SampleLog
{
    using Where = std::source_location;
    std::vector<double> values;

    void scalar(double value, [[maybe_unused]] Where at = Where::current())
    {
        values.push_back(value);
    }
    // Any x/y/z vector, float or double.
    template <typename V> void vec(const V &v, Where at = Where::current())
    {
        scalar(v.x, at);
        scalar(v.y, at);
        scalar(v.z, at);
    }
    void floats(const float *data, int count, Where at = Where::current())
    {
        for (int i = 0; i < count; ++i)
            scalar(data[i], at);
    }
};
