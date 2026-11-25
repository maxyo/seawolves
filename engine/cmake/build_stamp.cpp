// The build's commit in a kept symbol (cmake/build_stamp.cmake).
#include "storm_build_stamp.h"

extern "C" __attribute__((used, visibility("default"))) const char storm_build_stamp[] =
    "storm-build-stamp:" STORM_BUILD_COMMIT;
