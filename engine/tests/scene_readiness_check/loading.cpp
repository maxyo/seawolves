// The stock loading screen and transaction as the scripts' ReloadProgressEnd runs them: the core's transaction
// (core/src/loading_scene.hpp), the renderer's loading screen (renderer/src/progress_view.hpp) and the native call
// (location/src/reload_progress.hpp), with a renderer, a core and the scene's readiness as doubles. No GPU or game.
#include "core_fake.hpp"
#include "loading_scene.hpp"
#include "progress_view.hpp"
#include "reload_progress.hpp"
#include "ship_fake.hpp"

#include <catch2/catch.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
storm::test::FakeCore fake_core;
uint32_t now = 1000;     // SDL_GetTicks
uint32_t present_ms = 0; // how long a Present takes
int waits = 0;           // SDL_Delay calls

// The scene being loaded, as the renderer's reveal reports it.
struct Scene
{
    storm::render::SceneLoadStatus status;
    bool error = false;              // a mandatory resource failed: the reveal never completes
    int pending_until = 4;           // the reveal holds until this many loading frames were drawn
    int completions_kept_active = 0; // CompleteSceneReveal calls that leave the load active (its pipelines wait)
    int completions = 0, abandons = 0, cpu_ends = 0, cpu_suspends = 0;
} scene;

// The loading screen's renderer: textures are numbers, a Present advances the clock.
struct TestRenderer : SERVICE
{
    static constexpr uint32_t kProgressPresentGapMs = 50;
    char image[32] = "", back[32] = "", tips[32] = "";
    char *progressImage = image, *progressBackImage = back, *progressTipsImage = tips;
    int32_t progressImageSize = 32, progressBackImageSize = 32, progressTipsImageSize = 32;
    int32_t progressTexture = -1, backTexture = -1, back0Texture = -1, progressTipsTexture = -1;
    int32_t loadFrame = 0, progressSafeCounter = 0;
    bool isInPViewProcess = false, bInsideScene = true;
    uint32_t progressUpdateTime = 0, progressPresentGap = kProgressPresentGapMs, progressPresents = 0,
             progressPresentMs = 0, progressShownAt = 0;
    float progressFramesPosX = 0.85f, progressFramesPosY = 0.8f, progressFramesWidth = 0.0625f,
          progressFramesHeight = 0.0625f, m_fHeightDeformator = 1;
    int32_t progressFramesCountX = 8, progressFramesCountY = 8;
    struct Device
    {
        int presents = 0, presenting = 0;
        void Present()
        {
            ++presents;
            presenting += present_ms;
            now += present_ms;
        }
    } device;
    Device *d3d9 = &device;

    int32_t next_texture = 0;
    bool progress_on_create = false; // a texture load reports progress (the scripts' ProgressView)
    std::vector<std::string> created, missing;
    std::vector<int32_t> released;
    int frames = 0;   // RunStart: the loading transaction's frames
    int overlays = 0; // the loading picture drawn over a frame (DrawProgressView(false))
    int spinners = 0; // the loading picture drawn at all

    int32_t TextureCreate(const char *name)
    {
        created.emplace_back(name);
        if (progress_on_create)
            ProgressView();
        return std::ranges::find(missing, name) == missing.end() ? ++next_texture : -1;
    }
    void TextureRelease(int32_t texture)
    {
        released.push_back(texture);
    }
    bool released_texture(int32_t texture) const
    {
        return std::ranges::find(released, texture) != released.end();
    }
    void TextureSet(int32_t, int32_t)
    {
    }
    template <class... Args> void DrawPrimitiveUP(D3DPRIMITIVETYPE, uint32_t, uint32_t, Args...)
    {
    }
    void GetViewport(D3DVIEWPORT9 *viewport)
    {
        *viewport = {.X = 0, .Y = 0, .Width = 1280, .Height = 720, .MinZ = 0, .MaxZ = 1};
    }
    void BeginScene()
    {
    }
    void EndScene()
    {
    }
    bool DrawFrameStats()
    {
        d3d9->Present();
        return true;
    }

    void StartProgressView()
    {
        storm::render::LoadingScreen::start(*this);
    }
    void ProgressView()
    {
        storm::render::LoadingScreen::progress(*this);
    }
    void DrawProgressView(bool present)
    {
        const auto frame = loadFrame;
        storm::render::LoadingScreen::draw(*this, present);
        if (loadFrame != frame)
        {
            ++spinners;
            overlays += !present;
        }
    }
    void EndProgressView()
    {
        ReleaseProgressView();
    }
    void ReleaseProgressView()
    {
        storm::render::LoadingScreen::release(*this);
    }
    void RunStart() override
    {
        ++frames;
    }
    void RunEnd() override
    {
        storm::render::LoadingScreen::run_end(*this);
    }
};

struct Animation : SERVICE
{
    int poses = 0;
    void RunStart() override
    {
        REQUIRE(storm::loading::active);
        ++poses;
    }
};

// A loaded entity: what the transaction asks of it.
struct TestEntity : Entity
{
    bool resource = false;
    int prepared = 0, simulated = 0;
    bool Init() override
    {
        return true;
    }
    void ProcessStage(Stage stage, uint32_t delta) override
    {
        REQUIRE(delta == 0);
        if (stage == Stage::realize || stage == Stage::execute)
            ++simulated;
        if (resource && stage == Stage::loading_prepare)
            ++prepared;
    }
};

// The core's side of the transaction.
struct TestCore
{
    TestRenderer renderer;
    Animation animation;
    TestEntity resource, dialog, weather;
    std::vector<entid_t> realize_ids;
    std::vector<std::string> traces;
    TIMER Timer;
    struct Scripts
    {
        int aborts = 0;
        void AbortExecution()
        {
            ++aborts;
        }
    } scripts;
    Scripts *Compiler = &scripts;
    bool Exit_flag = false;
    struct RealizeLayers
    {
        uint32_t in = 0, frozen = 0;
    };
    struct Entities
    {
        RealizeLayers owner; // the realize layers of entity 5, the scene's owner
        RealizeLayers GetRealizeLayers(entid_t id) const
        {
            return id == 5 ? owner : RealizeLayers{};
        }
    } entity_manager_;
    uint64_t deferred_scene_generation_ = 0;

    TestCore()
    {
        EntityManager::SetId(resource, 1);
        EntityManager::SetId(dialog, 2);
        EntityManager::SetId(weather, 3);
        resource.resource = weather.resource = true;
        realize_ids = {1, 2};
        renderer.progressTexture = 1;
    }
    void Trace(const char *format, ...)
    {
        char line[512];
        va_list list;
        va_start(list, format);
        std::vsnprintf(line, sizeof(line), format, list);
        va_end(list);
        traces.emplace_back(line);
    }
    bool traced(std::string_view text) const
    {
        return std::ranges::count_if(
                   traces, [&](const std::string &line) { return line.find(text) != std::string::npos; }) == 1;
    }
    void *GetService(const char *name)
    {
        return std::string_view(name) == "renderer" ? static_cast<SERVICE *>(&renderer)
                                                    : static_cast<SERVICE *>(&animation);
    }
    const std::vector<entid_t> &GetEntityIds(layer_type_t) const
    {
        return realize_ids;
    }
    bool IsEntityValid(entid_t id) const
    {
        return id != 99;
    }
    Entity *GetEntityPointerSafe(entid_t id)
    {
        return id == 1 ? &resource : id == 2 ? static_cast<Entity *>(&dialog) : &weather;
    }
    void ForEachEntity(const std::function<void(Entity *)> &each)
    {
        for (Entity *entity :
             {static_cast<Entity *>(&resource), static_cast<Entity *>(&dialog), static_cast<Entity *>(&weather)})
            each(entity);
    }
    bool PrepareLoadingScene()
    {
        return storm::loading::SceneTransaction::prepare(*this);
    }
    bool LoadingSceneDeferred() const
    {
        return storm::loading::SceneTransaction::deferred(*this);
    }
    bool ReloadProgressEnd()
    {
        return storm::loading::ReloadProgressEnd<TestRenderer>(*this);
    }
    // An abort: the scripts unwound once, the engine exit requested, the outcome in the log (an ERROR unless the
    // player asked for it).
    bool aborted(const char *outcome, bool error) const
    {
        return scripts.aborts == 1 && Exit_flag && !traces.empty() &&
               traces.back().find(std::string("outcome=") + outcome) != std::string::npos &&
               traces.back().starts_with("ERROR: ") == error;
    }
};

// The host's side: the platform pump (lifecycle, Cancel) and the loading schedule.
struct Host
{
    int pumps = 0, suspend_for = 0, cancel_at = 0, wait_polls = 0;
    int starts = 0, presented = 0, suspended = 0, ended = 0;
    bool cancelled = false, invalidate_after_present = false;
    TestCore *core = nullptr;
} host;

void reset_scene()
{
    scene = {};
    scene.status = {.generation = 7, .active = true, .state = "preparing", .owner = 0};
    host = {};
    now = 1000;
    present_ms = 0;
    waits = 0;
}

// The registrations the engine makes at start-up, for every check here.
struct Registrations
{
    storm::platform::LoadingPlatformRegistration platform{[](uint64_t generation) {
        using storm::platform::LoadingPlatformState;
        REQUIRE(generation == 7);
        ++host.pumps;
        if (host.cancel_at && host.pumps == host.cancel_at)
        {
            host.cancelled = true;
            return LoadingPlatformState::Cancel;
        }
        return host.pumps <= host.suspend_for ? LoadingPlatformState::Suspended : LoadingPlatformState::Continue;
    }};
    storm::platform::LoadingScheduleRegistration schedule{
        [](uint64_t generation, storm::platform::LoadingSchedulePhase phase) -> unsigned {
            using Phase = storm::platform::LoadingSchedulePhase;
            REQUIRE(generation == 7);
            REQUIRE(storm::platform::LoadingPlatformTransactionActive());
            if (phase == Phase::Poll && host.wait_polls)
            {
                --host.wait_polls;
                return 5;
            }
            if (phase == Phase::FrameStarted)
                ++host.starts;
            if (phase == Phase::FramePresented)
            {
                REQUIRE(host.core->renderer.device.presents == host.core->renderer.frames);
                ++host.presented;
                if (host.invalidate_after_present)
                    scene.status.generation = 8;
            }
            if (phase == Phase::Suspended)
                ++host.suspended;
            if (phase == Phase::Ended)
                ++host.ended;
            return 0;
        }};
} registrations;
} // namespace

Core &core = fake_core;

extern "C" Uint32 SDL_GetTicks(void)
{
    return now;
}
extern "C" void SDL_Delay(Uint32 ms)
{
    REQUIRE(ms <= 2);
    ++waits;
}

namespace storm::render
{
SceneLoadStatus GetSceneLoadStatus()
{
    return scene.status;
}
bool HoldSceneReveal()
{
    return scene.error || (host.core ? host.core->renderer.frames : 0) < scene.pending_until;
}
void CompleteSceneReveal()
{
    REQUIRE_FALSE(HoldSceneReveal());
    scene.status.active = scene.completions < scene.completions_kept_active;
    ++scene.completions;
}
void AbandonSceneLoad(uint64_t generation)
{
    REQUIRE(generation == 7);
    scene.status.active = false;
    ++scene.abandons;
}
void SuspendSceneLoad(uint64_t generation)
{
    REQUIRE(generation == 7);
    ++scene.cpu_suspends;
}
void EndSceneCpuBudget(uint64_t generation)
{
    REQUIRE(generation == 7);
    ++scene.cpu_ends;
}
void ArmSceneCover(bool)
{
}
} // namespace storm::render

TEST_CASE("a progress frame inside the loading transaction presents nothing; the transaction's frame presents once")
{
    reset_scene();
    TestCore core;
    host.core = &core;
    auto &renderer = core.renderer;
    {
        storm::loading::Scope loading;
        renderer.ProgressView(); // a texture load inside the transaction's frame, e.g. under a reflection target
        REQUIRE(renderer.device.presents == 0);
        REQUIRE(renderer.spinners == 0);
        renderer.RunEnd();
        REQUIRE(renderer.device.presents == 1);
        REQUIRE(renderer.overlays == 1);
        REQUIRE_FALSE(storm::loading::ready);
        REQUIRE(renderer.progressTexture == 1); // the artwork stays while the scene loads
    }
    now += 1000;
    renderer.ProgressView();
    REQUIRE(renderer.device.presents == 2); // outside it, a progress frame presents
}

TEST_CASE("the loading screen's artwork lives from StartProgressView to the transaction's end")
{
    reset_scene();
    scene.pending_until = 1000;
    TestCore core;
    host.core = &core;
    auto &renderer = core.renderer;
    renderer.progressTexture = -1;
    std::string("first").copy(renderer.image, 5);
    std::string("background").copy(renderer.back, 10);
    std::string("tip").copy(renderer.tips, 3);
    renderer.StartProgressView();
    const int32_t spinner = renderer.progressTexture, old_back = renderer.backTexture,
                  old_tip = renderer.progressTipsTexture;
    REQUIRE(spinner > 0);
    {
        storm::loading::Scope loading;
        for (int frame = 0; frame < 80; ++frame)
        {
            renderer.RunStart();
            renderer.RunEnd();
        }
        // Held frames neither run the ordinary 50-frame cleanup nor release the artwork.
        REQUIRE(renderer.overlays == 80);
        REQUIRE(renderer.device.presents == 80);
        REQUIRE(renderer.released.empty());
        REQUIRE_FALSE(storm::loading::ready);
        // A new load while the old one owns its artwork: the spinner stays, the scene's pictures are replaced (no tip).
        std::string("second").copy(renderer.image, 7);
        renderer.tips[0] = 0;
        renderer.StartProgressView();
        REQUIRE(renderer.progressTexture == spinner);
        REQUIRE(renderer.progressTipsTexture == -1);
        REQUIRE(renderer.released_texture(old_back));
        REQUIRE(renderer.released_texture(old_tip));
        REQUIRE(renderer.created.back() == "second");
        scene.pending_until = 0;
        renderer.RunStart();
        renderer.RunEnd();
        REQUIRE(storm::loading::ready);
        REQUIRE(renderer.progressTexture == spinner);
        storm::render::CompleteSceneReveal();
    }
    renderer.EndProgressView();
    REQUIRE(renderer.progressTexture == -1);
    REQUIRE(renderer.backTexture == -1);
    REQUIRE(renderer.back0Texture == -1);
}

TEST_CASE("outside a transaction the loading picture goes after 50 frames; a frame without it still presents")
{
    reset_scene();
    scene.status.active = false;
    TestCore core;
    host.core = &core;
    auto &renderer = core.renderer;
    renderer.progressTexture = -1;
    renderer.StartProgressView();
    for (int frame = 0; frame < 51; ++frame)
        renderer.RunEnd();
    REQUIRE(renderer.progressTexture == -1);
    REQUIRE(renderer.overlays == 50);
    REQUIRE(renderer.device.presents == 51);

    TestRenderer empty;
    empty.RunEnd();
    REQUIRE(empty.device.presents == 1);
    REQUIRE(empty.overlays == 0);
    empty.ProgressView(); // no loading screen up: nothing to show
    REQUIRE(empty.device.presents == 1);
    REQUIRE(empty.spinners == 0);
}

TEST_CASE("the progress a texture load reports while the loading screen loads its own artwork shows nothing")
{
    reset_scene();
    TestRenderer renderer;
    renderer.progressTexture = 1;
    std::string("background").copy(renderer.back, 10);
    renderer.progress_on_create = true;
    renderer.StartProgressView();
    REQUIRE(renderer.back0Texture > 0);
    REQUIRE(renderer.device.presents == 0);
}

TEST_CASE("without the full-screen picture the scripts set, the stock one is taken; without the spinner, none")
{
    reset_scene();
    TestRenderer renderer;
    std::string("loading\\sea.tga").copy(renderer.back, 15);
    renderer.missing = {"loading\\sea.tga"};
    renderer.StartProgressView();
    REQUIRE(renderer.back0Texture > 0);
    REQUIRE(renderer.created.back() == "loading\\ImgBack.tga");
    renderer.ReleaseProgressView();

    TestRenderer without;
    without.missing = {"Loading\\progress.tga"};
    without.StartProgressView();
    REQUIRE(without.progressTexture == -1);
    REQUIRE(without.back0Texture == -1); // nothing else is loaded for a screen that cannot show
    REQUIRE(fake_core.traces.back().starts_with("ERROR: "));
}

// A load of `resources` creations, `work` ms each, each calling ProgressView, on a display whose Present takes
// `present` ms: its length, and at most a fifth of it presenting (give or take the last frame).
uint32_t load(uint32_t resources, uint32_t work, uint32_t present, TestRenderer &renderer)
{
    reset_scene();
    present_ms = present;
    renderer.progressTexture = 1;
    renderer.back0Texture = 2;
    renderer.progressUpdateTime = now - 1000; // StartProgressView: the first progress frame at once
    const uint32_t start = now;
    for (uint32_t i = 0; i < resources; ++i)
    {
        renderer.ProgressView();
        now += work;
    }
    const uint32_t total = now - start;
    REQUIRE(renderer.device.presents >= 1);
    REQUIRE(renderer.device.presenting * 5 <= total + 5 * present);
    return total;
}

TEST_CASE("progress frames between pieces of loading work take at most a fifth of the load")
{
    // ~450 creations of 2 ms on a display that takes 66 ms a frame (the emulator's sea-to-map trip): presenting every
    // 50 ms from one present's start would run the load at the display's frame rate.
    TestRenderer slow;
    REQUIRE(load(450, 2, 66, slow) <= 900 * 5 / 4 + 66);
    TestRenderer fast;
    REQUIRE(load(450, 2, 16, fast) <= 900 * 5 / 4 + 16);
    TestRenderer long_work;
    load(40, 30, 33, long_work);
    // The stock rate stays: a fast display shows a progress frame about every 50 ms of work, not more often.
    TestRenderer floor;
    const uint32_t total = load(100, 10, 1, floor);
    REQUIRE(floor.device.presents >= 15);
    REQUIRE(floor.device.presents <= total / TestRenderer::kProgressPresentGapMs + 1);
}

TEST_CASE("ReloadProgressEnd prepares the scene behind the loading screen, then the screen goes")
{
    reset_scene();
    TestCore core;
    host.core = &core;
    core.Timer.Carry = 99;
    auto &renderer = core.renderer;
    REQUIRE(core.ReloadProgressEnd());
    REQUIRE(renderer.frames >= 4);
    REQUIRE(renderer.overlays == renderer.frames);
    REQUIRE(renderer.device.presents == renderer.frames);
    REQUIRE(core.animation.poses == renderer.frames);
    REQUIRE(core.resource.prepared + core.weather.prepared == 2 * renderer.frames);
    REQUIRE(core.resource.simulated + core.dialog.simulated + core.weather.simulated == 0);
    REQUIRE(renderer.progressTexture == -1); // EndProgressView after the reveal
    REQUIRE(scene.completions == 1);
    REQUIRE_FALSE(storm::loading::active);
    REQUIRE(core.Timer.Carry == 0); // the loading time is not handed to the next game frame
    REQUIRE(host.starts == renderer.frames);
    REQUIRE(host.presented == renderer.frames);
    REQUIRE(host.ended == 1);
    REQUIRE(scene.cpu_ends == 1);
    REQUIRE_FALSE(storm::platform::LoadingPlatformTransactionActive());
    REQUIRE(core.scripts.aborts == 0);
    REQUIRE_FALSE(core.Exit_flag);
}

TEST_CASE("a load still active after its reveal keeps the transaction; none: the screen goes")
{
    reset_scene();
    scene.completions_kept_active = 2;
    TestCore core;
    host.core = &core;
    REQUIRE(core.ReloadProgressEnd());
    REQUIRE(scene.completions == 3);
    REQUIRE(core.renderer.frames == scene.pending_until + 2);
    REQUIRE(host.ended == 1);

    reset_scene();
    scene.status.active = false;
    TestCore idle;
    host.core = &idle;
    REQUIRE(idle.ReloadProgressEnd());
    REQUIRE(idle.renderer.frames == 0);
    REQUIRE(host.ended == 0);
    REQUIRE(idle.renderer.progressTexture == -1);
}

TEST_CASE("a suspended app and the schedule's waits pump the host without drawing")
{
    reset_scene();
    host.suspend_for = 3;
    TestCore core;
    host.core = &core;
    REQUIRE(core.ReloadProgressEnd());
    REQUIRE(host.pumps == core.renderer.frames + 3);
    REQUIRE(host.suspended == 3);
    REQUIRE(scene.cpu_suspends == 3);
    REQUIRE(host.ended == 1);

    reset_scene();
    host.wait_polls = 3;
    TestCore waiting;
    host.core = &waiting;
    REQUIRE(waiting.ReloadProgressEnd());
    REQUIRE(host.pumps == waiting.renderer.frames + 3);
    REQUIRE(waits == 3);
    REQUIRE(host.starts == waiting.renderer.frames);
    REQUIRE_FALSE(waiting.Exit_flag);
}

TEST_CASE("Cancel on the loading screen aborts the load: the scripts unwind and the engine exits")
{
    reset_scene();
    host.wait_polls = 8;
    host.cancel_at = 3;
    TestCore core;
    host.core = &core;
    REQUIRE_FALSE(core.ReloadProgressEnd());
    REQUIRE(core.aborted("cancelled", false));
    REQUIRE(host.cancelled);
    REQUIRE(core.renderer.frames == 0);
    REQUIRE(waits == 2);
    REQUIRE(host.ended == 1);
    REQUIRE(scene.cpu_ends == 1);

    // A held load (a resource failed) cancelled after two frames: the artwork stays for the engine's exit.
    reset_scene();
    scene.error = true;
    host.cancel_at = 3;
    TestCore held;
    host.core = &held;
    REQUIRE_FALSE(held.ReloadProgressEnd());
    REQUIRE(held.aborted("cancelled", false));
    REQUIRE(held.renderer.frames == 2);
    REQUIRE(held.renderer.device.presents == 2);
    REQUIRE(held.renderer.released.empty());
    REQUIRE_FALSE(storm::loading::active);
    REQUIRE(held.Timer.Carry == 0);
    REQUIRE(host.ended == 1);
    REQUIRE(scene.cpu_ends == 1);
    REQUIRE_FALSE(storm::platform::LoadingPlatformTransactionActive());
}

TEST_CASE("a load replaced or nested inside the transaction aborts it loudly")
{
    reset_scene();
    host.invalidate_after_present = true;
    TestCore core;
    host.core = &core;
    REQUIRE_FALSE(core.ReloadProgressEnd());
    REQUIRE(core.aborted("invalidated", true));
    REQUIRE(core.renderer.frames == 1);
    REQUIRE(core.renderer.released.empty());
    REQUIRE(host.ended == 1);
    REQUIRE(scene.cpu_ends == 1);

    reset_scene();
    TestCore nested;
    host.core = &nested;
    {
        storm::loading::Scope outer;
        REQUIRE_FALSE(nested.PrepareLoadingScene());
    }
    REQUIRE(nested.aborted("reentrant", true));
    REQUIRE(nested.renderer.frames == 0);
    REQUIRE(host.ended == 0);
}

TEST_CASE("without the stock artwork the location still loads, with an ERROR")
{
    reset_scene();
    TestCore core;
    host.core = &core;
    core.renderer.progressTexture = -1;
    REQUIRE(core.ReloadProgressEnd());
    REQUIRE_FALSE(core.Exit_flag);
    REQUIRE(core.renderer.frames >= 4);
    REQUIRE(core.renderer.device.presents == core.renderer.frames);
    REQUIRE(host.ended == 1);
    REQUIRE(fake_core.traces.back().starts_with("ERROR: "));
}

TEST_CASE("a scene whose owner was deleted before its reveal is abandoned and the game goes on")
{
    reset_scene();
    scene.status.owner = 99;
    scene.pending_until = 1000;
    TestCore core;
    host.core = &core;
    REQUIRE(core.ReloadProgressEnd());
    REQUIRE(scene.abandons == 1);
    REQUIRE(core.scripts.aborts == 0);
    REQUIRE_FALSE(core.Exit_flag);
    REQUIRE(core.renderer.frames == 0);
    REQUIRE(host.ended == 1);
    REQUIRE(scene.cpu_ends == 1);
    REQUIRE(core.traces.back().starts_with("ERROR: "));

    reset_scene();
    scene.status.owner = 5;
    TestCore valid;
    host.core = &valid;
    REQUIRE(valid.ReloadProgressEnd());
    REQUIRE(scene.abandons == 0);
    REQUIRE(valid.renderer.frames >= 4);
}

TEST_CASE("a scene the scripts froze is prepared at the ReloadProgressEnd after they let it draw")
{
    // A world-map save quick-loaded at sea: wdmCreateWorldMap's nested ReloadProgressEnd runs while the sea still
    // freezes REALIZE, the only layer the map draws in. The load is deferred (logged once) with the screen up.
    reset_scene();
    scene.status.owner = 5;
    TestCore core;
    host.core = &core;
    core.entity_manager_.owner = {.in = 0b10, .frozen = 0b10};
    REQUIRE(core.ReloadProgressEnd());
    REQUIRE(core.renderer.frames == 0);
    REQUIRE(core.renderer.released.empty());
    REQUIRE(scene.status.active);
    REQUIRE(host.ended == 0);
    REQUIRE_FALSE(storm::loading::active);
    REQUIRE(core.ReloadProgressEnd());
    REQUIRE(core.renderer.frames == 0);
    REQUIRE(core.renderer.released.empty());
    REQUIRE(core.traced("deferred generation=7"));
    core.entity_manager_.owner.frozen = 0;
    REQUIRE(core.ReloadProgressEnd());
    REQUIRE(core.renderer.frames >= 4);
    REQUIRE(core.renderer.progressTexture == -1);
    REQUIRE_FALSE(scene.status.active);

    // One of its realize layers draws (a location over a frozen sea), or it is in none (SEA_AI): prepared at once.
    for (const TestCore::RealizeLayers layers :
         {TestCore::RealizeLayers{.in = 0b1010, .frozen = 0b1000}, TestCore::RealizeLayers{.in = 0, .frozen = 0}})
    {
        reset_scene();
        scene.status.owner = 5;
        TestCore drawing;
        host.core = &drawing;
        drawing.entity_manager_.owner = layers;
        REQUIRE(drawing.ReloadProgressEnd());
        REQUIRE(drawing.renderer.frames >= 4);
        REQUIRE(drawing.renderer.progressTexture == -1);
    }
}
