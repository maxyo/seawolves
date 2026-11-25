#pragma once
#include <cstdint>
namespace bgfx
{
struct TextureHandle
{
    uint16_t idx;
};
} // namespace bgfx
#define BGFX_INVALID_HANDLE {UINT16_MAX}
