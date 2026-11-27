// The texture tier of the game's DXT textures (objects.cpp) as seen by other textures made from pixels.
#pragma once

#include "../texture/core/formats.hpp"

#include <optional>

namespace storm::shim
{
// The format a 2D colour texture made from RGBA8 pixels takes on this GPU: BC or ETC2 (the tier of the game's DXT 2D
// textures, logged once), with or without `alpha`; nothing on the RGBA8 tier.
std::optional<texture::GpuFormat> compressed_2d_format(bool alpha);
} // namespace storm::shim
