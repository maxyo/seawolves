// The scripts' ReloadProgressEnd (location_script_lib.cpp) over the core and renderer types, so that
// tests/scene_readiness_check runs it with its own.
#pragma once

namespace storm::loading
{
// The stock loading transaction, then the loading screen goes, unless the scene still waits for the scripts to let it
// draw (a nested ReloadProgressEnd: the screen stays up). False: the transaction aborted, the scripts are unwound and
// the engine exits.
template <class Renderer, class Core> bool ReloadProgressEnd(Core &core)
{
    if (!core.PrepareLoadingScene())
        return false;
    if (core.LoadingSceneDeferred())
        return true;
    auto *rs = static_cast<Renderer *>(core.GetService("renderer"));
    if (rs)
        rs->EndProgressView();
    return true;
}
} // namespace storm::loading
