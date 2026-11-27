// The pictures of the game as the pack draws them. Needs nothing of RmlUi or the router: the script screens' props
// (script_props.cpp) use it in the VM's host checks too.
#pragma once

#include "storm_ui/interface_model.h"

#include <nlohmann/json.hpp>

namespace storm::ui::screens
{
using Json = nlohmann::json;

// The unified row image {kind:"item", texture:"atlas:GROUP/Picture"} for a picture the
// engine names by its pictures.ini group, {kind:"portrait", texture:"INTERFACES/PORTRAITS/
// 128/face_N"} for a game portrait, {kind:"item", texture:"<name under RESOURCE/Textures>"} for any
// other game texture; null when the widget carries no such reference.
Json PictureOf(const ImageRef *image);
// {texture, uv} for <ui-game-image> (c-map-view, backdrops): a texture file of the game with
// the rectangle shown ("interfaces/maps/kayman", "0,0,1,1") or an atlas picture ("atlas:GROUP/
// Picture", ""); null for any other picture.
Json TextureOf(const ImageRef *image);
} // namespace storm::ui::screens
