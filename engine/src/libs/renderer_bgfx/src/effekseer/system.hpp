// The device side of the Effekseer system (storm/render/effekseer.hpp), for the shim.
#pragma once

namespace storm::render::effekseer
{
// Before bgfx shuts down: releases the renderer, the manager and every loaded effect.
void ShutdownDevice();
} // namespace storm::render::effekseer
