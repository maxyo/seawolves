// The build's watermark (watermark.hpp, cmake/build_stamp.cmake).
#include "storm_build_stamp.h"
#include "watermark.hpp"

#ifndef STORM_BUILD_WATERMARK_STRING
#error "The build sets no STORM_BUILD_PLATFORM (cmake/android.cmake, cmake/host.cmake)"
#endif

namespace storm
{
const char kBuildWatermark[] = STORM_BUILD_WATERMARK_STRING;
}
