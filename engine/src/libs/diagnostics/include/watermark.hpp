#pragma once
// The build's watermark, "<platform>_<commit>": defined by cmake/build_watermark.cpp, the one file that includes the
// generated build stamp (cmake/build_stamp.cmake), so a new commit or a modified tree recompiles that file only.
namespace storm
{
extern const char kBuildWatermark[];
}
