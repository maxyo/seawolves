// The script VM of the host checks is built without the build stamp (cmake/build_stamp.cmake).
#include "watermark.hpp"

namespace storm
{
const char kBuildWatermark[] = "host_checks";
}
