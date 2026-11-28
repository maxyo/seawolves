// What the host reports about its frames: the traces of the flicker check (Config::trace_actions, action_trace.h),
// the slow frame line and one line per screen switch.
#include "host_impl.h"

#include <cstdio>

namespace storm::ui
{

std::vector<ActionTrace> HostImpl::TakeActionTraces(bool finish_open)
{
    if (finish_open)
        Traced(tracer_.Finish(true));
    return std::exchange(traces_, {});
}

// The content on the screen: the screen or plain document and the HUD (toasts and the tooltip pass over
// everything and are not part of what an action shows).
FrameContent HostImpl::TraceContent(std::string shown)
{
    Rml::ElementDocument *const documents[] = {document_, hud_document_};
    return CaptureContent(documents, float(config_.width), float(config_.height), std::move(shown),
                          system_.GetElapsedTime());
}

// A player's action starts with what the screen shows now: the frame on the screen, unless something changed
// since it was drawn (then that change is the first thing the action shows).
void HostImpl::BeginTrace(const std::string &what)
{
    Traced(tracer_.Begin(what, TraceContent(last_frame_.shown), float(config_.width), float(config_.height)));
}

void HostImpl::Traced(std::optional<ActionTrace> trace)
{
    if (!trace)
        return;
    const std::string line = DescribeTrace(*trace);
    if (trace->transient || trace->late)
        Rml::Log::Message(Rml::Log::LT_WARNING, "%s", line.c_str());
    else if (config_.log_taps || trace->transient)
        Log(line);
    traces_.push_back(std::move(*trace));
    if (traces_.size() > 512)
        traces_.erase(traces_.begin());
}

// A drawn frame whose UI work alone took kSlowFrameMs or more, with its split. At most one line
// every two seconds; a screen switch has its own report.
void HostImpl::ReportSlowFrame()
{
    constexpr double kSlowFrameMs = 25.0;
    const FrameStats &f = counters_;
    const auto now = Clock::now();
    if (f.update_ms + f.render_ms < kSlowFrameMs || Milliseconds(slow_reported_, now) < 2000.0)
        return;
    slow_reported_ = now;
    char line[512];
    std::snprintf(line, sizeof line,
                  "UI slow frame: update %.1f + render %.1f ms | apply %.1f, rebuild %.1f (%d, %d elements "
                  "touched), style %.1f, layout %.1f (%d boxes, %d passes), pictures %d in %.1f, render incl. %d "
                  "meshes, textures %d (%zu KB) in %.1f",
                  f.update_ms, f.render_ms, f.apply_ms, f.rebuild_ms, f.rebuilds, f.touched, f.style_ms, f.layout_ms,
                  f.laid_out, f.layout_passes, f.pictures, f.picture_ms, f.geometry, f.textures, f.texture_bytes / 1024,
                  f.texture_ms);
    Log(line);
}

// One "UI switch" line per screen switch: from the tap (or the interface opening) to the screen's
// first frame and on until its frames stop rebuilding or laying out, with their CPU by stage.
void HostImpl::ReportSwitch(bool idle)
{
    if (!switch_.open || switch_.modelled == Clock::time_point{})
        return;
    const auto now = Clock::now();
    if (!idle)
    {
        const FrameStats &frame = counters_;
        if (!switch_.drawn)
        {
            switch_.drawn = true;
            switch_.first = now;
            switch_.first_frame = frame;
        }
        else
        {
            FrameStats &sum = switch_.settle;
            sum.update_ms += frame.update_ms, sum.render_ms += frame.render_ms, sum.apply_ms += frame.apply_ms;
            sum.rebuild_ms += frame.rebuild_ms, sum.style_ms += frame.style_ms, sum.layout_ms += frame.layout_ms;
            sum.picture_ms += frame.picture_ms, sum.texture_ms += frame.texture_ms, sum.pictures += frame.pictures;
            sum.textures += frame.textures, sum.texture_bytes += frame.texture_bytes, sum.rebuilds += frame.rebuilds;
            sum.laid_out += frame.laid_out, sum.layout_passes += frame.layout_passes;
        }
        ++switch_.frames;
        const bool busy = frame.rebuilds > 0 || frame.laid_out > 0 || frame.pictures > 0 || frame.textures > 0;
        if ((busy || switch_.frames == 1) && switch_.frames < 30)
            return;
    }
    const auto ms = [](Clock::time_point from, Clock::time_point to) { return Milliseconds(from, to); };
    const bool acted = last_tap_ > reported_ && last_tap_ <= switch_.begun;
    const Clock::time_point origin = acted ? last_tap_ : switch_.begun;
    const FrameStats &f = switch_.first_frame, &s = switch_.settle;
    char line[1024];
    std::snprintf(
        line, sizeof line,
        "UI switch %s: first frame %.0f ms after %s%s (game: %s%.0f ms to the model), settled %.0f ms after it in %d "
        "more frames | first frame CPU: apply %.1f, rebuild %.1f (%d, %d "
        "elements touched), style %.1f, layout %.1f (%d boxes, %d passes), pictures %d in %.1f, render %.1f incl. "
        "%d meshes, textures %d (%zu KB) in %.1f | settling CPU: update %.1f, render %.1f, rebuild %d, layout %.1f "
        "(%d passes), pictures %d in %.1f",
        switch_.screen.c_str(), ms(origin, switch_.first), acted ? "the tap" : "the interface opened", "",
        switch_.expected ? ("interface opened +" + std::to_string(int(ms(origin, switch_.begun))) + " ms, ").c_str()
                         : "",
        ms(origin, switch_.modelled), ms(switch_.first, now), switch_.frames - 1, f.apply_ms, f.rebuild_ms, f.rebuilds,
        f.touched, f.style_ms, f.layout_ms, f.laid_out, f.layout_passes, f.pictures, f.picture_ms, f.render_ms,
        f.geometry, f.textures, f.texture_bytes / 1024, f.texture_ms, s.update_ms, s.render_ms, s.rebuilds, s.layout_ms,
        s.layout_passes, s.pictures, s.picture_ms);
    Log(line);
    switch_.open = false;
    reported_ = now;
}

} // namespace storm::ui
