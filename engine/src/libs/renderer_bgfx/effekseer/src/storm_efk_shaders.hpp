// efkbgfx's shaders, embedded at build time (../CMakeLists.txt); the patched renderer creates them through this.
#pragma once

#include <cstdint>

namespace EffekseerRendererBGFX
{
// The vertex or fragment shader of a renderer shader name ("sprite_adv_lit", "model_unlit", ...) for the running
// backend (its bgfx handle index); bgfx::kInvalidHandle when there is none. Sprites share the fragment shaders of the
// models.
uint16_t LoadShader(const char *name, bool vertex);
} // namespace EffekseerRendererBGFX
