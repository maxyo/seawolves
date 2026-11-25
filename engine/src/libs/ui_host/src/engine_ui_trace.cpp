#include "engine_ui.hpp"

namespace storm::android::ui
{
void EngineUi::FrameTrace::OnUiRender(const storm::render::FrameContext &frame)
{
    EngineUi &ui = EngineUi::Get();
    ui.TraceInput("frame", fmt::format("{} at {:.1f} ms", frame.frame_index, MonotonicMs()));
    if (!ui.host_)
        return;
    const storm::ui::FrameStats stats = ui.host_->LastFrameStats();
    ++frames;
    if (stats.number != counted) // a frame the host made (not the last one replayed unchanged)
    {
        counted = stats.number;
        // The frame timeline: every frame the host made, with what it showed and what it cost.
        const double now_ms = MonotonicMs();
        TraceInputs().debug(
            "loop {}: made frame {} at {:.1f} ms (+{:.1f}): shows {} after {} held | update {:.1f} = apply "
            "{:.1f} + rebuild {:.1f} ({}, {} touched) + style {:.1f} + layout {:.1f} ({} boxes, {} passes) + "
            "pictures {:.1f} ({}), render {:.1f} ({} meshes, {} textures)",
            ui.loop_, stats.number, now_ms, made_ms ? now_ms - made_ms : 0.0, stats.shown, stats.held, stats.update_ms,
            stats.apply_ms, stats.rebuild_ms, stats.rebuilds, stats.touched, stats.style_ms, stats.layout_ms,
            stats.laid_out, stats.layout_passes, stats.picture_ms, stats.pictures, stats.render_ms, stats.geometry,
            stats.textures);
        made_ms = now_ms;
        ++made;
        update_ms += stats.update_ms;
        render_ms += stats.render_ms;
        draws += stats.draws;
        layers += stats.layers;
        layouts += stats.layout_passes;
        touched += stats.touched;
        boxes += stats.laid_out;
        meshes += stats.geometry;
    }
    const uint32_t now = SDL_GetTicks();
    if (!since)
        since = now;
    if (now - since < 5000)
        return;
    const double per = made ? double(made) : 1.0;
    TraceInputs().debug("{} UI frames in {:.1f} s, {} made (the rest replayed unchanged): per made frame CPU "
                        "update {:.2f} ms + render {:.2f} ms, {:.0f} draws, {:.1f} layers, {:.1f} elements "
                        "touched, {:.0f} boxes laid out, {:.1f} meshes made; {} layout passes",
                        frames, (now - since) / 1000.0, made, update_ms / per, render_ms / per, double(draws) / per,
                        double(layers) / per, double(touched) / per, double(boxes) / per, double(meshes) / per,
                        layouts);
    frames = made = 0, update_ms = render_ms = 0.0, draws = layers = layouts = touched = boxes = meshes = 0,
    since = now;
}

void EngineUi::Prewarmer::OnFrameEnd(const storm::render::FrameContext &)
{
    EngineUi &ui = EngineUi::Get();
    if (!ui.host_)
        return;
    // Behind the loading screen of a scene load (its transaction) that has gone on for kPrewarmAfterMs: its
    // frames wait for workers, uploads and the quality products' time budget. Not in a short reload (a few
    // frames that the steps would stretch), not behind the title's (its reveal counts frames: the graphics
    // preset's start) nor the scripts' own progress frames (InitGame): there it would delay what the player
    // waits for.
    const std::string &screen = ui.presentation_.screen;
    const auto now = std::chrono::steady_clock::now();
    if (!storm::loading::active)
        load_started.reset();
    else if (!load_started)
        load_started = now;
    const bool hidden = load_started && screen != "mainmenu" &&
                        std::chrono::duration<double, std::milli>(now - *load_started).count() >= kPrewarmAfterMs;
    if (!hidden && storm::loading::screen_up)
        return;
    if (!hidden && !screen.empty() && screen != "mainmenu")
        return;                                                            // an open screen is what the frames are for
    ui.host_->Prewarm(hidden ? kPrewarmHiddenMs : kPrewarmIdleMs, hidden); // nothing left: returns at once
}
} // namespace storm::android::ui
