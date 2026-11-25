// The reveal of a scene loaded behind the loading picture (effects/scene_reveal.hpp), driven frame by frame in the
// order the engine calls it: BeginScene, the scene's draws, the loading transaction's hold, the cover, the Present and
// the frame's sample.
#include "effects/scene_reveal.hpp"
#include "../renderer_bgfx/log_lines.hpp"
#include "storm/render/loading_budget.hpp"

#include <catch2/catch.hpp>

#include <initializer_list>
#include <string>
#include <vector>

using storm::effects::SceneProduct;
using storm::effects::SceneReveal;
using storm::effects::SceneSample;
using storm::effects::SceneTexture;
using storm::effects::ShadowState;
using State = SceneReveal::State;
using spdlog::level::err;
using spdlog::level::info;

namespace
{
struct Lines
{
    std::vector<std::pair<spdlog::level::level_enum, std::string>> lines;
    Lines()
    {
        storm::host_check::CollectLog([this](auto level, std::string_view text) { lines.emplace_back(level, text); });
    }
    ~Lines()
    {
        storm::host_check::StopCollectingLog();
    }
    size_t count(spdlog::level::level_enum level, std::string_view text) const
    {
        size_t n = 0;
        for (const auto &[l, t] : lines)
            n += l == level && t.find(text) != std::string::npos;
        return n;
    }
};

struct FrameOptions
{
    bool cover = true;           // the loading picture's full-screen layer is drawn
    bool cover_armed = true;     // ... armed as the cover
    bool cover_full = true;      // ... covering the whole target
    bool cover_blend_add = true; // ... with no blending or ADD blending
    bool picture = true;         // the scene draws into the game picture
    bool scene = true;           // the device drew a 3D scene this frame
    bool replaced = false;       // the picture's target is replaced before the Present
    bool post_swap = false;      // the post chain swaps the picture into its other target
    bool post_wrong = false;     // ... from a target the frame did not begin with
    bool pipelines = false;      // Vulkan pipelines still being made
    bool stable = true;          // the Present advanced on the same surface and back buffer
    bool stable_target = true;   // the frame began on the same back buffer as the last
    bool sampled_twice = false;  // the frame's sample comes twice (a Present that did not advance the frame id)
};

// A load and its clock, one loading frame at a time.
struct Loading
{
    SceneReveal reveal;
    uint64_t now = 1000, frame_id = 0;
    SceneSample sample;
    uint32_t hidden = 0; // frames whose picture was drawn hidden

    explicit Loading(const char *scene)
    {
        reveal.begin(scene, 0, now);
    }
    // One loading frame `ms` after the last; returns whether the reveal was held at its hold.
    bool frame(uint64_t ms = 16, FrameOptions o = {})
    {
        now += ms;
        uint64_t target = 8;
        reveal.begin_frame(++frame_id, 5, target, o.stable_target);
        if (o.picture)
        {
            if (reveal.hide_picture(5, target))
                ++hidden;
            else
                reveal.picture_submitted();
        }
        if (o.post_swap)
        {
            reveal.post_target(5, o.post_wrong ? 7 : target, 9);
            target = 9;
        }
        const bool held = reveal.hold(now, sample, o.pipelines);
        reveal.arm_cover(o.cover_armed);
        if (o.cover)
            reveal.cover_draw_submitted(o.cover_full, o.cover_blend_add);
        reveal.arm_cover(false);
        reveal.check_target(5, o.replaced ? 77 : target);
        reveal.presented(true, o.stable);
        reveal.sample(now, sample, o.scene, frame_id);
        if (o.sampled_twice)
            reveal.sample(now, sample, o.scene, frame_id);
        reveal.end_frame();
        return held;
    }
    // Frames until the reveal is granted (at most `limit`); 0 when it never was.
    uint32_t frames_to_reveal(uint32_t limit = 100, uint64_t ms = 16, FrameOptions o = {})
    {
        for (uint32_t i = 1; i <= limit; ++i)
            if (!frame(ms, o))
                return i;
        return 0;
    }
};
// A texture without a GPU copy whose transcode runs; `step` changes its stage.
SceneTexture Uploading(uint64_t serial, uint32_t step = 0)
{
    return {.serial = serial,
            .name = "locations/wall",
            .fallback = bool(step & 1),
            .transcoding = true,
            .dirty = bool(step & 2)};
}
SceneTexture Uploaded(uint64_t serial)
{
    return {.serial = serial, .name = "locations/wall", .drawable = true, .full_chain = true};
}
// The frames a location needs once nothing is pending: one drawn hidden on the old sample, kFullFrames full ones, and
// the hold that grants it.
constexpr uint32_t kRevealFrames = SceneReveal::kFullFrames + 2;
} // namespace

TEST_CASE("a location is revealed once its mandatory textures are uploaded and three full frames followed")
{
    Lines log;
    Loading l("location/Pirates_town");
    l.sample.textures = {Uploading(1)};
    for (int i = 0; i < 20; ++i)
        REQUIRE(l.frame());
    REQUIRE(l.reveal.textures() == 1);
    REQUIRE(l.hidden > 0); // under the presented cover the pending scene is drawn for its resources only
    l.sample.textures = {Uploaded(1)};
    REQUIRE(l.frames_to_reveal() == kRevealFrames);
    REQUIRE(l.reveal.state() == State::Ready);
    REQUIRE(log.count(info, "location readiness: ready location/Pirates_town generation=1 after") == 1);
    REQUIRE(l.reveal.complete());
    REQUIRE(l.reveal.state() == State::Idle);
}

TEST_CASE("a texture without a GPU copy holds while any work on it is left")
{
    for (const SceneTexture t : {SceneTexture{.serial = 1, .name = "a", .fallback = true},
                                 SceneTexture{.serial = 1, .name = "a", .transcoding = true},
                                 SceneTexture{.serial = 1, .name = "a", .dirty = true}})
    {
        Loading l("location/Pirates_town");
        l.sample.textures = {t};
        REQUIRE(l.frames_to_reveal(20) == 0);
        REQUIRE(l.reveal.textures() == 1);
    }
    Loading l("location/Pirates_town");
    l.sample.textures = {SceneTexture{.serial = 1, .name = "a"}}; // no copy and no work: it failed, it never holds
    REQUIRE(l.frames_to_reveal() == kRevealFrames);
}

TEST_CASE("a location waits for its Vulkan pipelines")
{
    Loading l("location/Pirates_town");
    REQUIRE(l.frames_to_reveal(50, 16, {.pipelines = true}) == 0);
    REQUIRE(l.frames_to_reveal() == kRevealFrames);
}

TEST_CASE("a texture first used in a frame: that frame does not count and the next one is drawn hidden")
{
    Loading l("location/Pirates_town");
    REQUIRE(l.frame());
    REQUIRE(l.frame()); // one full frame
    const uint32_t hidden = l.hidden;
    l.reveal.begin_frame(++l.frame_id, 5, 8, true);
    REQUIRE_FALSE(l.reveal.hide_picture(5, 8));
    l.reveal.picture_submitted();
    l.reveal.demand(); // a draw found it; it has a GPU copy already, so nothing holds
    l.reveal.arm_cover(true);
    l.reveal.cover_draw_submitted(true, true);
    l.reveal.arm_cover(false);
    l.reveal.presented(true, true);
    l.reveal.sample(l.now, l.sample, true, l.frame_id);
    l.reveal.end_frame();
    REQUIRE(l.frames_to_reveal() == kRevealFrames);
    REQUIRE(l.hidden == hidden + 1);
}

TEST_CASE("a texture demanded after the reveal was granted keeps the loading picture")
{
    Loading l("location/Pirates_town");
    REQUIRE(l.frames_to_reveal() == kRevealFrames);
    l.reveal.demand();
    REQUIRE(l.reveal.state() == State::Preparing);
    REQUIRE_FALSE(l.reveal.complete());
    const uint32_t hidden = l.hidden;
    REQUIRE(l.frame());
    REQUIRE(l.hidden == hidden + 1); // rediscovered hidden
}

TEST_CASE("a frame found pending after the reveal was granted keeps the loading picture")
{
    Loading l("location/Pirates_town");
    REQUIRE(l.frames_to_reveal() != 0);
    l.sample.switching = true; // a settings switch begins before the transaction ended
    l.frame();
    REQUIRE(l.reveal.state() == State::Preparing);
    REQUIRE_FALSE(l.reveal.complete());
    l.sample.switching = false;
    REQUIRE(l.frames_to_reveal() != 0);
    REQUIRE(l.reveal.complete());
}

TEST_CASE("once ready, a frame without the cover still counts")
{
    Loading l("location/Pirates_town");
    REQUIRE(l.frames_to_reveal() == kRevealFrames);
    for (int i = 0; i < 3; ++i)
        REQUIRE_FALSE(l.frame(16, {.cover = false}));
    REQUIRE(l.reveal.state() == State::Ready);
}

TEST_CASE("the quality products hold a location at most kSceneProductBudgetMs after its mandatory resources")
{
    using storm::effects::kSceneProductBudgetMs;
    SECTION("a slow product: revealed at the budget with it still in progress")
    {
        Lines log;
        Loading l("location/Pirates_town");
        l.sample.products.add(SceneProduct::Relief, 3);
        const uint64_t start = l.now;
        REQUIRE(l.frames_to_reveal(1000, 100) != 0);
        REQUIRE(l.now - start >= kSceneProductBudgetMs);
        REQUIRE(l.now - start < kSceneProductBudgetMs + 1000);
        const std::string budget = std::to_string(kSceneProductBudgetMs);
        REQUIRE(log.count(info, "waited " + budget + " ms of " + budget +
                                    " for: relief/gloss maps 3 (budget reached); "
                                    "continued after the reveal: relief/gloss maps 3") == 1);
    }
    SECTION("a fast product: revealed when it is done")
    {
        Loading l("location/Pirates_town");
        l.sample.products.add(SceneProduct::Place);
        for (int i = 0; i < 10; ++i)
            REQUIRE(l.frame(100));
        l.sample.products = {};
        REQUIRE(l.frames_to_reveal(10, 100) != 0);
    }
    SECTION("the budget starts at mandatory-ready and is not restarted by a texture first used later")
    {
        Loading l("location/Pirates_town");
        l.sample.products.add(SceneProduct::Upscale);
        l.sample.textures = {Uploading(1)};
        for (int i = 0; i < 30; ++i)
            REQUIRE(l.frame(1000)); // 30 s of mandatory work: the products' budget has not begun
        l.sample.textures = {};
        const uint64_t ready = l.now;
        const uint64_t step = kSceneProductBudgetMs / 5;
        for (int i = 0; i < 2; ++i)
            REQUIRE(l.frame(step));
        l.sample.textures = {Uploading(2)};
        REQUIRE(l.frame(step));
        l.sample.textures = {};
        REQUIRE(l.frames_to_reveal(100, 100) != 0);
        // Begun at the first frame after `ready`; restarted by the late texture (three steps in) it would end later
        // than this.
        REQUIRE(l.now - ready < 3 * step + kSceneProductBudgetMs);
    }
    SECTION("the budget starts at a scene frame, not at loading frames without a scene")
    {
        Loading l("location/Pirates_town");
        l.sample.products.add(SceneProduct::Relief);
        for (int i = 0; i < 10; ++i)
            REQUIRE(l.frame(1000, {.scene = false}));
        const uint64_t scene = l.now;
        REQUIRE(l.frames_to_reveal(100, 100) != 0);
        REQUIRE(l.now - scene >= kSceneProductBudgetMs);
    }
    SECTION("the title does not wait for products")
    {
        Loading l("mainmenu");
        l.sample.products.add(SceneProduct::Relief, 40);
        REQUIRE(l.frames_to_reveal(10) != 0);
    }
}

TEST_CASE("the title holds the loading picture at most kTitleRevealBudgetMs")
{
    using storm::effects::kTitleRevealBudgetMs;
    Lines log;
    Loading l("mainmenu");
    uint64_t first_hold = 0;
    SECTION("while preparing, its pipelines included")
    {
        // A texture that keeps progressing (no stall release) and pipelines that never finish.
        uint64_t held_until = 0;
        for (uint32_t i = 0; i < 40; ++i)
        {
            l.sample.textures = {Uploading(1, i)};
            if (!l.frame(500, {.pipelines = true}))
                break;
            if (!first_hold)
                first_hold = l.now;
            held_until = l.now;
        }
        REQUIRE(held_until - first_hold < kTitleRevealBudgetMs);
        REQUIRE(l.now - first_hold >= kTitleRevealBudgetMs);
        REQUIRE(log.count(err, "location readiness: mainmenu not ready within its budget of 15000 ms (preparing, "
                               "pipelines pending): shown with what it has") == 1);
    }
    SECTION("while failed")
    {
        l.sample.sun = ShadowState::Failed;
        REQUIRE(l.frame(1000));
        first_hold = l.now;
        REQUIRE(l.reveal.state() == State::Failed);
        while (l.now - first_hold < kTitleRevealBudgetMs - 1000)
            REQUIRE(l.frame(1000));
        REQUIRE_FALSE(l.frame(1000));
        REQUIRE(log.count(err, "(failed): shown with what it has") == 1);
    }
    SECTION("counted from the first hold, not from the synchronous load before it")
    {
        l.now += 2 * kTitleRevealBudgetMs; // the models and the interface load before the transaction starts
        l.sample.textures = {Uploading(1)};
        REQUIRE(l.frame());
        first_hold = l.now;
        for (uint32_t i = 0; l.now - first_hold < kTitleRevealBudgetMs - 1000; ++i)
        {
            l.sample.textures = {Uploading(1, i)};
            REQUIRE(l.frame(1000));
        }
        REQUIRE_FALSE(l.frame(1000));
    }
    REQUIRE(l.reveal.state() == State::Expired);
    REQUIRE_FALSE(l.frame()); // nothing holds it again
    REQUIRE(l.reveal.complete());
    REQUIRE_FALSE(l.reveal.preparing());
}

TEST_CASE("a location has no budget: it is held while it prepares or failed")
{
    using storm::effects::kTitleRevealBudgetMs;
    Lines log;
    Loading l("location/Pirates_town");
    for (uint32_t i = 0; i < 10 * kTitleRevealBudgetMs / 1000; ++i)
    {
        l.sample.textures = {Uploading(1, i)};
        REQUIRE(l.frame(1000));
    }
    l.sample.textures = {};
    l.sample.lamps = ShadowState::Failed;
    for (int i = 0; i < 20; ++i)
        REQUIRE(l.frame(1000));
    REQUIRE(l.reveal.state() == State::Failed);
    REQUIRE(log.count(err, "location readiness: location/Pirates_town resource preparation failed") == 1);
    REQUIRE_FALSE(l.reveal.complete());
    l.sample.lamps = ShadowState::Ready; // the failure cleared: full frames reveal it
    REQUIRE(l.frames_to_reveal() != 0);
}

TEST_CASE("only frames drawn into the target they began with and presented stable count")
{
    Loading l("location/Pirates_town");
    SECTION("a target replaced before the Present")
    {
        REQUIRE(l.frames_to_reveal(50, 16, {.replaced = true}) == 0);
    }
    SECTION("a post chain swap from another target")
    {
        REQUIRE(l.frames_to_reveal(50, 16, {.post_swap = true, .post_wrong = true}) == 0);
    }
    SECTION("a frame without the scene's picture")
    {
        REQUIRE(l.frames_to_reveal(50, 16, {.picture = false}) == 0);
    }
    SECTION("a Present on a replaced surface or back buffer")
    {
        REQUIRE(l.frames_to_reveal(50, 16, {.stable = false}) == 0);
    }
    SECTION("a frame begun on a re-created back buffer")
    {
        REQUIRE(l.frames_to_reveal(50, 16, {.stable_target = false}) == 0);
    }
    SECTION("a frame sampled twice counts once")
    {
        REQUIRE(l.frames_to_reveal(50, 16, {.sampled_twice = true}) == kRevealFrames);
        return;
    }
    SECTION("the post chain's swap keeps the frame")
    {
        REQUIRE(l.frames_to_reveal(10, 16, {.post_swap = true}) == kRevealFrames);
        return;
    }
    SECTION("a settings change drops the frames counted, also once ready")
    {
        REQUIRE(l.frames_to_reveal() == kRevealFrames);
        l.reveal.invalidate();
        REQUIRE(l.reveal.state() == State::Preparing);
        REQUIRE_FALSE(l.reveal.complete());
    }
    REQUIRE(l.frames_to_reveal() != 0);
}

TEST_CASE("a frame is drawn hidden only under a cover presented in the frame before")
{
    Loading l("location/Pirates_town");
    l.sample.textures = {Uploading(1)};
    SECTION("no cover drawn yet")
    {
        for (int i = 0; i < 5; ++i)
            l.frame(16, {.cover = false});
        REQUIRE(l.hidden == 0);
    }
    SECTION("a cover presented on an unstable surface")
    {
        l.frame(16, {.stable = false});
        l.frame();
        REQUIRE(l.hidden == 0);
    }
    SECTION("a settings change drops the presented cover")
    {
        l.frame();
        l.reveal.invalidate();
        l.frame();
        REQUIRE(l.hidden == 0);
    }
    SECTION("a cover presented")
    {
        l.frame();
        l.frame();
        REQUIRE(l.hidden == 1);
    }
}

TEST_CASE("without a presented cover the frames count only after kCoverFrames loading frames")
{
    Lines log;
    SECTION("no cover")
    {
        Loading l("location/Pirates_town");
        const uint32_t frames = l.frames_to_reveal(100, 16, {.cover = false});
        REQUIRE(frames > SceneReveal::kCoverFrames);
        REQUIRE(frames <= SceneReveal::kCoverFrames + SceneReveal::kFullFrames + 1);
        REQUIRE(log.count(err, "no opaque loading cover presented in 30 loading frames") == 1);
    }
    SECTION("a draw not armed as the cover, not covering the target, or blending that can keep the destination")
    {
        for (const FrameOptions o : {FrameOptions{.cover_armed = false}, FrameOptions{.cover_full = false},
                                     FrameOptions{.cover_blend_add = false}})
        {
            Loading l("location/Pirates_town");
            REQUIRE(l.frames_to_reveal(100, 16, o) > SceneReveal::kCoverFrames);
        }
    }
    SECTION("a cover once presented is never waived")
    {
        Loading l("location/Pirates_town");
        l.sample.textures = {Uploading(1)};
        REQUIRE(l.frame());
        for (int i = 0; i < 100; ++i)
            REQUIRE(l.frame(16, {.cover = false}));
        l.sample.textures = {};
        REQUIRE(l.frames_to_reveal(100, 16, {.cover = false}) == 0);
        REQUIRE(log.count(err, "no opaque loading cover") == 0);
    }
}

TEST_CASE("a texture upload without progress stops holding after kTextureStallMs, reported once")
{
    Lines log;
    Loading l("location/Pirates_town");
    l.sample.textures = {Uploading(7)};
    REQUIRE(l.frame());
    const uint64_t start = l.now;
    while (l.now - start < SceneReveal::kTextureStallMs - 1000)
        REQUIRE(l.frame(1000));
    REQUIRE(l.reveal.textures() == 1);
    l.sample.textures = {Uploading(7, 1)}; // it progressed: the budget starts again
    REQUIRE(l.frame(1000));
    const uint64_t progressed = l.now;
    while (l.now - progressed < SceneReveal::kTextureStallMs - 1000)
        REQUIRE(l.frame(1000));
    REQUIRE(log.count(err, "made no upload progress") == 0);
    REQUIRE(l.frames_to_reveal(10, 1000) != 0);
    REQUIRE(l.reveal.textures() == 0);
    REQUIRE(log.count(err, "location readiness: location/Pirates_town: texture locations/wall made no upload "
                           "progress for 60000 ms") == 1);
}

TEST_CASE("a preparation without any change is an ERROR once, naming what is pending")
{
    Lines log;
    Loading l("location/Pirates_town");
    l.sample.sun = ShadowState::Pending; // a shadow pass that is never drawn
    for (uint64_t t = 0; t < 3 * SceneReveal::kSlowMs; t += 10000)
        REQUIRE(l.frame(10000));
    REQUIRE(log.count(err, "location readiness: slow preparation location/Pirates_town") == 1);
    REQUIRE(log.count(err, "sun shadows") == 1);
}

TEST_CASE("any stage of a scene texture changing is progress")
{
    using Change = void (*)(SceneTexture &);
    for (const Change change : std::initializer_list<Change>{
             [](SceneTexture &x) { x.drawable = !x.drawable; }, [](SceneTexture &x) { x.fallback = !x.fallback; },
             [](SceneTexture &x) { x.transcoding = !x.transcoding; }, [](SceneTexture &x) { x.dirty = !x.dirty; },
             [](SceneTexture &x) { x.full_chain = !x.full_chain; }, [](SceneTexture &x) { ++x.relief; },
             [](SceneTexture &x) { ++x.gloss; }, [](SceneTexture &x) { ++x.upscale; }})
    {
        Lines log;
        Loading l("location/Pirates_town");
        l.sample.sun = ShadowState::Pending;
        l.sample.textures = {Uploaded(2)};
        for (uint64_t t = 0; t < 3 * SceneReveal::kSlowMs; t += 10000)
        {
            change(l.sample.textures[0]);
            REQUIRE(l.frame(10000));
        }
        REQUIRE(log.count(err, "slow preparation") == 0);
    }
}

TEST_CASE("the time the transaction is suspended counts towards no budget or report")
{
    using storm::effects::kSceneProductBudgetMs;
    using storm::effects::kTitleRevealBudgetMs;
    Lines log;
    SECTION("the slow report and the texture stall")
    {
        Loading l("location/Pirates_town");
        l.sample.textures = {Uploading(1)};
        REQUIRE(l.frame());
        l.reveal.pause(l.now);
        REQUIRE(l.frame(3 * SceneReveal::kSlowMs)); // back after six minutes in the background
        REQUIRE(log.count(err, "slow preparation") == 0);
        REQUIRE(log.count(err, "made no upload progress") == 0);
        REQUIRE(l.reveal.textures() == 1);
    }
    SECTION("the title's budget")
    {
        Loading l("mainmenu");
        l.sample.textures = {Uploading(1)};
        REQUIRE(l.frame());
        l.reveal.pause(l.now);
        REQUIRE(l.frame(2 * kTitleRevealBudgetMs));
    }
    SECTION("the products' wait")
    {
        Loading l("location/Pirates_town");
        l.sample.products.add(SceneProduct::Relief);
        REQUIRE(l.frame());
        REQUIRE(l.frame());
        l.reveal.pause(l.now);
        REQUIRE(l.frame(2 * kSceneProductBudgetMs));
        // The wait goes on from where it was: half of it is still not its end.
        REQUIRE(l.frames_to_reveal(kSceneProductBudgetMs / 200, 100) == 0);
    }
}

TEST_CASE("a new load replaces the one in progress")
{
    Loading l("location/A");
    SceneReveal &reveal = l.reveal;
    const uint64_t first = reveal.generation();
    REQUIRE(l.frame()); // the cover is presented: the next frame is eligible
    reveal.begin_frame(99, 5, 8, true);
    REQUIRE_FALSE(reveal.hide_picture(5, 8));
    const uint64_t second = reveal.begin("sea", 4, 10);
    REQUIRE(second == first + 1);
    REQUIRE(reveal.owner() == 4);
    reveal.picture_submitted();
    reveal.presented(true, true);
    REQUIRE_FALSE(reveal.frame_full()); // the frame begun under the old load never counts
    reveal.end();
    REQUIRE_FALSE(reveal.preparing());
    REQUIRE_FALSE(reveal.hold(20, {}, false));
}

TEST_CASE("the loading frames are paced at twice their GPU time from their start, within its bounds")
{
    storm::render::LoadingBudget pacing;
    REQUIRE(pacing.deadline(100, true, 20, 100) == 100); // no frame started yet
    pacing.frame_started(100);
    REQUIRE(pacing.deadline(110, true, 20, 110) == 140);  // from the frame's start, its CPU work included
    REQUIRE(pacing.deadline(145, true, 20, 145) == 145);  // a late frame waits no more
    REQUIRE(pacing.deadline(170, true, 100, 170) == 180); // at most kMaxIntervalMs from the start
    REQUIRE(pacing.deadline(180, true, 100, 180) == 180);
    REQUIRE(pacing.deadline(110, false, 20, 110) == 110); // nothing waits for the GPU
    REQUIRE(pacing.deadline(110, true, 20, 0) == 110);    // no GPU time
    REQUIRE(pacing.deadline(500, true, 20, 100) == 500);  // a stale one
    REQUIRE(pacing.deadline(110, true, 0, 110) == 110);   // an invalid one
}
