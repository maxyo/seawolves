#include "game_profile.hpp"

#include <mutex>
#include <optional>

#include <spdlog/spdlog.h>

#include "v_file_service.h"

namespace storm
{
namespace
{
std::mutex mutex;
std::optional<GameProfile> chosen;
} // namespace

GameProfile gameProfile()
{
    std::lock_guard lock(mutex);
    if (!chosen)
    {
        chosen = detectGameProfile([](const std::string &path) { return fio->_FileOrDirectoryExists(path.c_str()); },
                                   [](const std::string &reason) { spdlog::warn("Game profile: {}", reason); });
        spdlog::info("Game profile: {} (game directory {})", gameProfileFacts(*chosen).name,
                     fio->_GetExecutableDirectory());
    }
    return *chosen;
}

void setGameProfile(GameProfile profile)
{
    std::lock_guard lock(mutex);
    chosen = profile;
    spdlog::info("Game profile: {} (set by the caller)", gameProfileFacts(profile).name);
}
} // namespace storm
