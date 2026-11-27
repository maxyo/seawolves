// When a scene loaded behind the loading picture (a location, the sea, the world map, the title) is shown: what it
// waits for, for how long, and the one place that says "reveal now". Game thread; no bgfx, worker or engine calls:
// scene_reveal.cpp samples the producers and forwards the shim's frame events.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "storm/render/log.hpp"

namespace storm::shim
{
struct TexStore;
}

namespace storm::effects
{
// Owner decisions: a location waits at most this long for the quality products in progress once its mandatory
// resources are ready (the rest fades in after the reveal); the title holds the loading picture at most this long.
inline constexpr uint64_t kSceneProductBudgetMs = 1500;
inline constexpr uint64_t kTitleRevealBudgetMs = 15000;

// The quality products a load waits for. One the preset does not want is never counted (its owner makes nothing).
enum class SceneProduct : uint8_t
{
    Transcode, // a texture drawing its fallback while its full transcode runs (a cache read included)
    Relief,    // relief/gloss maps of the scene's loaded textures (cache read, generation, upload and fade)
    Upscale,   // neural x2 of the textures the residency plan chose (near the camera)
    Place,     // the place's probes: its visibility and AO volumes (cache load or bake)
    TownLight, // the town light: the lightmap set (cache load or bake) and its first composition
    Layers,    // the texture arrays of the layered materials drawn (cache read or made)
    Count
};
struct SceneProducts
{
    std::array<uint32_t, static_cast<size_t>(SceneProduct::Count)> count{};
    void add(SceneProduct p, uint32_t n = 1)
    {
        count[static_cast<size_t>(p)] += n;
    }
    uint32_t operator[](SceneProduct p) const
    {
        return count[static_cast<size_t>(p)];
    }
    bool any() const
    {
        return std::any_of(count.begin(), count.end(), [](uint32_t n) { return n != 0; });
    }
    // "relief/gloss maps 12, town light 1", or "nothing".
    std::string describe() const
    {
        static constexpr const char *kNames[] = {"texture transcodes", "relief/gloss maps",
                                                 "x2 textures",        "place probes (visibility/AO volume)",
                                                 "town light",         "layer arrays"};
        std::string text;
        for (size_t i = 0; i < count.size(); ++i)
            if (count[i])
                text += (text.empty() ? "" : ", ") + std::string(kNames[i]) + " " + std::to_string(count[i]);
        return text.empty() ? "nothing" : text;
    }
};

// A scene texture as one sample sees it (shim::TexStore and its relief, gloss and x2 entries); `name` is valid for
// that sample. Any change of its stages is progress.
struct SceneTexture
{
    uint64_t serial = 0;
    std::string_view name;
    bool drawable = false;    // it has a GPU copy (its fallback or a previous upload included)
    bool fallback = false;    // it draws its fallback while its full transcode runs
    bool transcoding = false; // its transcode job runs
    bool dirty = false;       // new contents wait for the next sync
    bool full_chain = false;  // its full mip chain is uploaded
    // The state of its relief map, gloss map and x2 (their entries' states, plus one; 0: none).
    uint8_t relief = 0, gloss = 0, upscale = 0;
    // Without any GPU copy and with work left: it holds the reveal.
    bool uploading() const
    {
        return !drawable && (fallback || transcoding || dirty);
    }
    uint32_t upload_stage() const
    {
        return uint32_t(drawable) | uint32_t(fallback) << 1 | uint32_t(transcoding) << 2 | uint32_t(dirty) << 3 |
               uint32_t(full_chain) << 4;
    }
    uint64_t stage() const
    {
        return upload_stage() | uint64_t(relief) << 8 | uint64_t(gloss) << 16 | uint64_t(upscale) << 24;
    }
};
enum class ShadowState : uint8_t
{
    Ready,
    Pending, // the first pass is still to be drawn
    Failed   // its target or buffers could not be made: the load needs a reload
};
// One sample of the producers.
struct SceneSample
{
    std::vector<SceneTexture> textures;
    ShadowState sun = ShadowState::Ready, lamps = ShadowState::Ready;
    bool switching = false; // a settings switch in progress
    SceneProducts products;
};

class SceneReveal
{
  public:
    enum class State : uint8_t
    {
        Idle,      // no load
        Preparing, // something mandatory is pending, the products are waited for, or too few full frames yet
        Failed,    // a mandatory resource failed: held (Cancel on the loading screen) unless the failure clears
        Ready,     // reveal now
        Expired    // the title's budget ran out: reveal now with what it has
    };
    // Full scene frames in a row with nothing pending before the reveal. The game thread builds frame N while the
    // render thread executes N-1, and a Vulkan pipeline first asked for in a frame leaves that draw out: a full frame
    // can need resources that exist only a frame or two later, and finds them pending there.
    static constexpr uint32_t kFullFrames = 3;
    // Loading frames after which the frames count without a presented opaque cover: a load whose transaction draws
    // none (a script path without the loading artwork, a cover draw left out) would otherwise never be revealed.
    // Frames, not time: the synchronous script work before the transaction draws nothing.
    static constexpr uint32_t kCoverFrames = 30;
    // A texture upload whose stage did not change this long stops holding the reveal (logged by name): a lost or
    // stuck transcode must not keep a location, which has no budget, behind the loading picture for good.
    static constexpr uint64_t kTextureStallMs = 60000;
    // A preparation in which nothing changed this long is an ERROR, once, naming what is pending: a location has no
    // budget, and its loading frames go on, so nothing else notices a load that never ends.
    static constexpr uint64_t kSlowMs = 120000;

    // A new load: the previous one ends unrevealed. Returns its generation (the loading screen's actions name it).
    uint64_t begin(std::string_view scene, uint64_t owner, uint64_t now)
    {
        const uint64_t generation = generation_ + 1;
        const Frame frame = frame_; // a load begun inside a frame keeps that frame's decision; it never counts
        *this = {};
        frame_ = frame;
        frame_.revoked = true;
        frame_.dirty = false;
        generation_ = generation;
        scene_ = scene;
        owner_ = owner;
        started_ = stall_at_ = progress_at_ = now;
        budget_ms_ = scene == "mainmenu" ? kTitleRevealBudgetMs : 0;
        // The title waits for its mandatory resources only: its menu is what the player waits for.
        products_done_ = scene == "mainmenu";
        state_ = State::Preparing;
        return generation_;
    }
    // Revealed or abandoned.
    void end()
    {
        state_ = State::Idle;
        detail_.clear();
        textures_ = shadows_ = failed_ = 0;
    }
    // Revealed: ends the load when the reveal was granted (Ready or Expired); false otherwise (a frame since the grant
    // found something pending: the transaction goes on).
    bool complete()
    {
        if (state_ != State::Ready && state_ != State::Expired)
            return false;
        end();
        return true;
    }
    // The loading transaction is suspended (the app in the background): its time is not counted by the budgets and
    // the stall and slow reports, from now until the next sample or hold.
    void pause(uint64_t now)
    {
        if (preparing() && !paused_)
        {
            paused_ = true;
            paused_at_ = now;
        }
    }

    // Something new is needed (a texture first used): this frame does not count and the next one rediscovers hidden.
    void demand()
    {
        frame_.dirty = true;
        restart_frames();
    }
    // A required resource went away: the full frames count again.
    void restart_frames()
    {
        full_frames_ = 0;
        if (state_ == State::Ready)
            state_ = State::Preparing;
    }
    // The renderer's settings or targets change between frames: no receipt of this frame or the cover survives.
    void invalidate()
    {
        cover_ = {};
        frame_.ordinary = frame_.eligible = frame_.presented = false;
        frame_.revoked = frame_.dirty = true;
        if (preparing())
            restart_frames();
    }

    // The loading cover: the stock artwork's full-screen layer (Renderer::DrawProgressView). It counts once a draw
    // armed for it was submitted covering the whole target with no blending or ADD blending (MIN, MAX and SUBTRACT
    // can keep the destination), and then presented.
    void arm_cover(bool armed)
    {
        cover_.armed = armed && preparing();
    }
    void cover_draw_submitted(bool full_coverage, bool blend_add)
    {
        if (cover_.armed && full_coverage && blend_add)
            cover_.submitted = true;
    }
    bool cover_presented() const
    {
        return preparing() && cover_.presented;
    }

    // The game picture of a frame. Under a presented cover, while something is pending, its draws are made for their
    // resources and discarded (hidden); a frame counts as full when the picture was drawn, not hidden, into the target
    // it began with (or the post chain's swap of it), and presented on a stable surface.
    void begin_frame(uint64_t id, uint64_t surface, uint64_t target, bool stable_target)
    {
        if (frame_.begun && frame_.id == id)
            return;
        const bool rediscover = frame_.dirty;
        bool covered = cover_.presented;
        if (!covered && preparing() && !cover_seen_ && cover_frames_ >= kCoverFrames)
        {
            covered = true;
            if (render::Once(cover_waived_))
                render::Log().error("location readiness: {}: no opaque loading cover presented in {} loading frames; "
                                    "the readiness no longer waits for it and the scene under preparation may show",
                                    scene_, kCoverFrames);
        }
        frame_ = {};
        frame_.id = id;
        frame_.surface = surface;
        frame_.target = target;
        frame_.ordinary = preparing() && stable_target;
        frame_.eligible = frame_.ordinary && covered;
        frame_.hidden = frame_.eligible && (holding_ || rediscover);
        frame_.begun = true;
    }
    // Also checked at the Present: a picture target replaced during the frame (a resize, a lost surface) means the
    // presented picture is not the one drawn.
    void check_target(uint64_t surface, uint64_t target)
    {
        if (frame_.begun && (frame_.surface != surface || frame_.target != target))
            frame_.revoked = true;
    }
    bool hide_picture(uint64_t surface, uint64_t target)
    {
        check_target(surface, target);
        return frame_.begun && frame_.hidden;
    }
    void picture_submitted()
    {
        if (preparing())
            frame_.picture = true;
    }
    // The post chain swapped the game picture into another target: the frame goes on there.
    void post_target(uint64_t surface, uint64_t before, uint64_t after)
    {
        if (!frame_.begun || frame_.surface != surface || frame_.target != before || !after)
            frame_.revoked = true;
        else
            frame_.target = after;
    }
    void presented(bool advanced, bool stable)
    {
        cover_.presented = preparing() && advanced && stable && cover_.submitted;
        cover_.armed = cover_.submitted = false;
        cover_seen_ = cover_seen_ || cover_.presented;
        frame_.presented = advanced && stable;
    }
    bool frame_full() const
    {
        return frame_.begun && (frame_.eligible || (state_ == State::Ready && frame_.ordinary)) && !frame_.hidden &&
               !frame_.dirty && !frame_.revoked && frame_.picture && frame_.presented;
    }
    // After the frame's sample: a Present that failed starts its next attempt afresh, also under the same frame id.
    void end_frame()
    {
        frame_.begun = false;
    }

    // A sample of the producers after a presented frame: `frame` its id, `scene_frame` a 3D scene was drawn.
    void sample(uint64_t now, const SceneSample &s, bool scene_frame, uint64_t frame)
    {
        if (!preparing())
            return;
        resume(now);
        take(now, s, frame && scene_frame);
        if (state_ == State::Expired || failed_)
            return;
        if (!frame || frame <= last_frame_)
            return;
        last_frame_ = frame;
        if (!scene_frame || !frame_full() || holding_)
        {
            restart_frames();
            return;
        }
        if (state_ != State::Ready && ++full_frames_ >= kFullFrames)
            state_ = State::Ready;
    }

    // Before every frame of the loading transaction is drawn: samples the producers again (they may have moved since
    // the last frame) and decides. True while the reveal is held. `pipelines`: the scene's Vulkan pipelines are still
    // being made. Says each outcome once.
    bool hold(uint64_t now, const SceneSample &s, bool pipelines)
    {
        if (!preparing())
            return false;
        resume(now);
        if (!holding_since_)
            holding_since_ = now; // the title's budget counts from here, not from the synchronous load before it
        if (cover_frames_ < kCoverFrames)
            ++cover_frames_;
        pipelines_ = pipelines;
        take(now, s, false);
        const uint64_t elapsed = now - started_;
        if (budget_ms_ && (state_ == State::Preparing || state_ == State::Failed) && now - holding_since_ >= budget_ms_)
        {
            render::Log().error(
                "location readiness: {} not ready within its budget of {} ms ({}{}): shown with what it "
                "has, the rest is made while it is shown: {}",
                scene_, budget_ms_, state_ == State::Failed ? "failed" : "preparing",
                pipelines_ ? ", pipelines pending" : "", detail_);
            state_ = State::Expired;
            granted_ = products_done_ = true;
        }
        const bool held = state_ == State::Preparing || state_ == State::Failed;
        if (!held && !granted_)
        {
            render::Log().info("location readiness: ready {} generation={} after {} ms: mandatory ready at {} ms; "
                               "quality products waited {} ms of {} for: {}{}; continued after the reveal: {}",
                               scene_, generation_, elapsed, (products_started_ ? ready_at_ : now) - started_,
                               waited_ms_, kSceneProductBudgetMs, waited_.describe(),
                               products_expired_ ? " (budget reached)" : "", products_last_.describe());
            granted_ = products_done_ = true;
        }
        if (state_ == State::Failed && render::Once(failed_reported_))
            render::Log().error("location readiness: {} resource preparation failed; overlay retained, no incomplete "
                                "reveal: {}",
                                scene_, detail_);
        if (state_ == State::Preparing && now - progress_at_ >= kSlowMs && render::Once(slow_reported_))
            render::Log().error("location readiness: slow preparation {} elapsed={} ms unchanged={} ms (loading screen "
                                "kept, Cancel available): {}",
                                scene_, elapsed, now - progress_at_, detail_);
        return held;
    }

    bool preparing() const
    {
        return state_ != State::Idle;
    }
    State state() const
    {
        return state_;
    }
    uint64_t generation() const
    {
        return generation_;
    }
    uint64_t owner() const
    {
        return owner_;
    }
    const std::string &scene() const
    {
        return scene_;
    }
    // The last sample: the textures in upload that hold the reveal, the failed mandatory resources.
    uint32_t textures() const
    {
        return textures_;
    }
    uint32_t failed() const
    {
        return failed_;
    }

  private:
    // The time of a suspension is added to every clock that counts towards a budget or a report.
    void resume(uint64_t now)
    {
        if (!paused_)
            return;
        paused_ = false;
        const uint64_t away = now - paused_at_;
        if (holding_since_)
            holding_since_ += away;
        if (products_started_)
            ready_at_ += away;
        stall_at_ += away;
        progress_at_ += away;
    }
    // Takes the producers' state: what holds the reveal now, the stalls, the progress, the products' wait.
    void take(uint64_t now, const SceneSample &s, bool scene_frame)
    {
        detail_.clear();
        textures_ = 0;
        stall_now_.clear();
        progress_now_.clear();
        for (const SceneTexture &t : s.textures)
        {
            progress_now_.emplace_back(t.serial, t.stage());
            if (!t.uploading() || stalled_.contains(t.serial))
                continue;
            stall_now_.emplace_back(t.serial, t.upload_stage());
            ++textures_;
            name("texture upload: " + std::string(t.name));
        }
        std::sort(stall_now_.begin(), stall_now_.end());
        std::sort(progress_now_.begin(), progress_now_.end());
        if (stall_now_.empty() || stall_now_ != stall_last_)
            stall_at_ = now;
        else if (now >= stall_at_ + kTextureStallMs)
            for (const SceneTexture &t : s.textures)
                if (t.uploading() && !stalled_.contains(t.serial))
                {
                    stalled_.insert(t.serial); // no longer in flight: said once
                    --textures_;
                    render::Log().error("location readiness: {}: texture {} made no upload progress for {} ms: drawn "
                                        "black until it arrives, the reveal does not wait",
                                        scene_, t.name, kTextureStallMs);
                }
        stall_last_.swap(stall_now_);
        shadows_ = failed_ = 0;
        shadow(s.sun, "sun shadows");
        shadow(s.lamps, "lamp shadows");
        switching_ = s.switching || pipelines_;
        if (progress_now_ != progress_last_ || shadows_ != progress_shadows_ || failed_ != progress_failed_ ||
            switching_ != progress_switching_)
        {
            progress_at_ = now;
            progress_last_.swap(progress_now_);
            progress_shadows_ = shadows_;
            progress_failed_ = failed_;
            progress_switching_ = switching_;
        }
        const bool mandatory = textures_ || shadows_ || switching_;
        const bool products = hold_products(scene_frame && !mandatory && !failed_, s.products, now);
        if (products)
            name("quality products: " + s.products.describe());
        holding_ = mandatory || products || failed_;
        if (state_ == State::Expired)
            return;
        if (failed_)
        {
            state_ = State::Failed;
            full_frames_ = 0;
        }
        else if (holding_)
            restart_frames();
    }
    // What is pending, for the log lines: a location starts with hundreds of textures in upload, the lines name the
    // first few.
    void name(const std::string &what)
    {
        if (detail_.size() < 384)
            detail_ += (detail_.empty() ? "" : "; ") + what;
    }
    void shadow(ShadowState state, const char *what)
    {
        if (state == ShadowState::Pending)
        {
            ++shadows_;
            name(what);
        }
        else if (state == ShadowState::Failed)
        {
            ++failed_;
            name(std::string(what) + " failed");
        }
    }
    // The products' budget starts at the first sample after a presented scene frame with nothing mandatory pending;
    // later mandatory work does not restart it, and nothing holds after the reveal was granted. True while they hold.
    bool hold_products(bool mandatory_ready, const SceneProducts &products, uint64_t now)
    {
        products_last_ = products;
        if (products_done_)
            return false;
        if (!products_started_)
        {
            if (!mandatory_ready)
                return false;
            products_started_ = true;
            ready_at_ = now;
        }
        if (!products.any())
            return false;
        for (size_t i = 0; i < products.count.size(); ++i)
            waited_.count[i] = std::max(waited_.count[i], products.count[i]);
        if (now >= ready_at_ + kSceneProductBudgetMs)
        {
            products_expired_ = products_done_ = true;
            waited_ms_ = kSceneProductBudgetMs;
            return false;
        }
        waited_ms_ = now - ready_at_;
        return true;
    }

    State state_ = State::Idle;
    uint64_t generation_ = 0, owner_ = 0, started_ = 0, budget_ms_ = 0, holding_since_ = 0;
    std::string scene_, detail_;
    uint32_t textures_ = 0, shadows_ = 0, failed_ = 0;
    bool switching_ = false, pipelines_ = false;
    bool holding_ = false; // the last sample found something pending: a covered frame is drawn hidden
    uint32_t full_frames_ = 0;
    uint64_t last_frame_ = 0;
    bool granted_ = false, failed_reported_ = false, slow_reported_ = false, cover_waived_ = false;
    bool paused_ = false;
    uint64_t paused_at_ = 0;

    struct Cover
    {
        bool armed = false, submitted = false, presented = false;
    } cover_;
    bool cover_seen_ = false;
    uint32_t cover_frames_ = 0; // loading transaction frames, up to kCoverFrames

    struct Frame
    {
        uint64_t id = 0, surface = 0, target = 0;
        bool begun = false, ordinary = false, eligible = false, hidden = false, dirty = false, revoked = false,
             picture = false, presented = false;
    } frame_;

    bool products_started_ = false, products_done_ = false, products_expired_ = false;
    uint64_t ready_at_ = 0, waited_ms_ = 0;
    SceneProducts waited_{}, products_last_{};

    // Textures in flight (serial, upload stage), sorted, at the last sample; the stalled ones no longer hold.
    std::vector<std::pair<uint64_t, uint32_t>> stall_last_, stall_now_;
    std::unordered_set<uint64_t> stalled_;
    uint64_t stall_at_ = 0;
    // Every texture's stages (serial, stage), sorted, at the last change, for the slow report.
    std::vector<std::pair<uint64_t, uint64_t>> progress_last_, progress_now_;
    uint32_t progress_shadows_ = 0, progress_failed_ = 0;
    bool progress_switching_ = false;
    uint64_t progress_at_ = 0;
};

// The glue (scene_reveal.cpp). Game thread.
SceneReveal &CurrentSceneReveal();
void BeginSceneResources(const char *scene, uint64_t owner);
void ObserveSceneTexture(const std::shared_ptr<shim::TexStore> &texture);
bool ScenePreparing();
// Before each loading frame is drawn (render::HoldSceneReveal): true while it is held. `pipelines`: the scene's
// Vulkan pipelines are still being made.
bool HoldSceneResources(bool pipelines);
// The title's budget ran out: nothing holds its reveal, its pipelines included.
bool SceneRevealExpired();
// After the presented frame's producers have pumped (Host::end_frame).
void SampleSceneFrame(bool scene_frame, uint32_t frame, ShadowState sun, ShadowState lamps);
// The shim's frame events (Device: BeginScene, draws, the post chain, Present).
void BeginSceneFrame(uint64_t frame, uint64_t surface, uint64_t target, bool stable_target);
bool HideScenePicture(uint64_t surface, uint64_t target);
void ScenePictureTarget(uint64_t surface, uint64_t target);
void ScenePictureSubmitted();
void ScenePostTarget(uint64_t surface, uint64_t before, uint64_t after);
void SceneCoverDrawSubmitted(bool full_coverage, bool blend_add);
void SceneFramePresented(bool advanced, bool stable);
// A settings change (Host::configure).
void InvalidateSceneFrames();
struct LoadingUploadScope;
LoadingUploadScope SceneUploadScope();
} // namespace storm::effects
