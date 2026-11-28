#include "land_protocol.hpp"

#include <spdlog/spdlog.h>

#include <mutex>
#include <set>
#include <string>

namespace storm::location
{

namespace
{
bool FirstTime(std::string key)
{
    static std::mutex mutex;
    static std::set<std::string> seen;
    const std::scoped_lock lock(mutex);
    return seen.insert(std::move(key)).second;
}
} // namespace

void ReportUnknownCommand(std::string_view receiver, std::string_view name)
{
    if (FirstTime(fmt::format("unknown {} {}", receiver, name)))
        spdlog::error("{}: unknown command '{}' ignored", receiver, name);
}

void ReportWithoutEffect(std::string_view command, std::string_view what)
{
    if (FirstTime(fmt::format("without effect {}", command)))
        spdlog::warn("{}: accepted; {} by this engine", command, what);
}

void ReportNotImplemented(std::string_view name, std::string_view without)
{
    if (FirstTime(fmt::format("not implemented {}", name)))
        spdlog::error("not implemented: {}; the game continues without {}", name, without);
}

} // namespace storm::location
