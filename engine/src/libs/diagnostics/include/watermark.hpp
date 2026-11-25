#pragma once
// The build's watermark, STORM_BUILD_WATERMARK_STRING ("<platform>_<commit>"): storm_build_stamp.h is generated at build
// time into an include directory of the build (cmake/build_stamp.cmake). Included by name, never by an
// absolute-path define: such a define differs per worktree and defeats the shared compiler cache.
#include "storm_build_stamp.h"
#ifndef STORM_BUILD_WATERMARK_STRING
#error "The build sets no STORM_BUILD_PLATFORM (cmake/android.cmake, cmake/host.cmake)"
#endif
