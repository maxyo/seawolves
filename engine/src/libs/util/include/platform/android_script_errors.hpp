#pragma once
#include <cstdint>
namespace storm::android
{
// Game-thread monotonic error serial, on every platform. A synchronous optional adapter (the land HUD's, a touch
// interface script) can reject a partially executed script event even when the interpreter logs and continues.
inline uint64_t scriptErrorEpoch = 0;
} // namespace storm::android
