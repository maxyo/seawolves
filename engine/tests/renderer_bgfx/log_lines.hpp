// The renderer's log lines in a check (storm/render/log.hpp): each one with its level and its text without the
// "[storm-bgfx] " tag, from any thread.
#pragma once
#include "storm/render/log.hpp"

#include <functional>
#include <memory>
#include <string_view>

#include <spdlog/sinks/callback_sink.h>

namespace storm::host_check
{
inline void CollectLog(std::function<void(spdlog::level::level_enum level, std::string_view text)> collect)
{
    storm::render::SetLogSinks(
        "check", {std::make_shared<spdlog::sinks::callback_sink_mt>([collect](const spdlog::details::log_msg &msg) {
            std::string_view text(msg.payload.data(), msg.payload.size());
            constexpr std::string_view kTag = "[storm-bgfx] ";
            if (text.starts_with(kTag))
                text.remove_prefix(kTag.size());
            collect(msg.level, text);
        })});
}
// Back to stderr.
inline void StopCollectingLog()
{
    storm::render::SetLogSinks({}, {});
}
} // namespace storm::host_check
