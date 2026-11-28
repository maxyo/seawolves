// The touch interface on the desktop, a development build only (engine/README.md, "Linux"): the mouse is one finger,
// Escape is Back, the pack and the scripts come from the layer installed beside the engine (layers/mobile-ui).
#ifndef __ANDROID__
#include "../engine_ui.hpp"
#include "app_ui_names.hpp"
#include "platform/android_mailbox.hpp"
#include "platform/storage.hpp"

#include <SDL2/SDL.h>

namespace storm::android::ui
{
namespace
{
using storm::ui::TouchPhase;

// Window points to the surface's pixels (a window on a high-density screen has more pixels than points).
std::pair<float, float> SurfacePixels(uint32_t window_id, float x, float y)
{
    const auto *owner = storm::render::FrameOwner::Get();
    SDL_Window *window = SDL_GetWindowFromID(window_id);
    int width = 0, height = 0;
    if (window)
        SDL_GetWindowSize(window, &width, &height);
    if (!owner || width <= 0 || height <= 0)
        return {x, y};
    const auto surface = owner->Surface();
    return {x * float(surface.width_px) / float(width), y * float(surface.height_px) / float(height)};
}

// Runs where SDL takes the event in (the game thread's pump): the mouse and Escape go to the touch interface only, as
// a phone's touches and its Back button never reach the game's own controls.
int Filter(void *, SDL_Event *event)
{
    EngineUi &ui = EngineUi::Get();
    const double now = MonotonicMs();
    switch (event->type)
    {
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT)
        {
            const auto [x, y] = SurfacePixels(event->button.windowID, float(event->button.x), float(event->button.y));
            ui.Touch(int(event->type == SDL_MOUSEBUTTONDOWN ? TouchPhase::Down : TouchPhase::Up), 0, x, y, now);
        }
        return 0;
    case SDL_MOUSEMOTION:
        if (event->motion.state & SDL_BUTTON_LMASK)
        {
            const auto [x, y] = SurfacePixels(event->motion.windowID, float(event->motion.x), float(event->motion.y));
            ui.Touch(int(TouchPhase::Move), 0, x, y, now);
        }
        return 0;
    case SDL_MOUSEWHEEL: {
        // A wheel step is a short drag of a second finger where the pointer is (lists scroll, nothing is tapped).
        int px = 0, py = 0;
        SDL_GetMouseState(&px, &py);
        const auto [x, y] = SurfacePixels(event->wheel.windowID, float(px), float(py));
        constexpr float kStepPx = 120.0f;
        const float dy = float(event->wheel.y) * kStepPx;
        ui.Touch(int(TouchPhase::Down), 1, x, y, now - 32.0);
        ui.Touch(int(TouchPhase::Move), 1, x, y + dy * 0.5f, now - 16.0);
        ui.Touch(int(TouchPhase::Move), 1, x, y + dy, now - 1.0);
        ui.Touch(int(TouchPhase::Up), 1, x, y + dy, now);
        return 0;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (event->key.keysym.sym != SDLK_ESCAPE)
            return 1;
        if (event->type == SDL_KEYDOWN && !event->key.repeat)
        {
            if (ui.WantsBack())
                ui.Post([](storm::ui::Host &host) { host.Back(); });
            else
                mailbox::pending.push_back(mailbox::Back{});
        }
        return 0;
    default:
        return 1;
    }
}
} // namespace

namespace ui_platform
{
bool Ready()
{
    // Once, before the first host: the game is in front and live (the desktop has no lifecycle of its own here), and
    // the mouse becomes the finger.
    static const bool ready = [] {
        storm::android::inputEpoch.set(true);
        EngineUi::Get().PlatformState("presentation", R"({"live":true})");
        SDL_SetEventFilter(&Filter, nullptr);
        Log().warn("touch interface on the desktop (development build): the mouse is one finger, Escape is Back");
        return true;
    }();
    return ready;
}

std::unique_ptr<storm::ui::FileSource> OpenPack()
{
    const auto root = storm::storage::folders().layers.mobileUi / "ui";
    if (!std::filesystem::is_regular_file(root / "pack.json"))
        throw std::runtime_error("the UI pack is missing: " + root.string() +
                                 " has no pack.json (the engine's build "
                                 "installs it with STORM_TOUCH_UI)");
    return storm::ui::DirectoryFiles(root.string());
}

void ClosePack()
{
}

void ShowTextEditor(const storm::ui::TextRequest &request)
{
    Log().error("text input is not available on the desktop: the field «{}» keeps its text", request.title);
    EngineUi::Get().Post(
        [token = request.token, text = request.text](storm::ui::Host &host) { host.SubmitText(token, text, false); });
}

void UiFailed(const std::string &reason)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Touch interface", reason.c_str(), nullptr);
}

void HudInput(const std::string &verb, const std::string &value)
{
    if (verb == app::hud_input::shortcut)
        mailbox::pending.push_back(mailbox::Shortcut{value});
    // A presence change moves the Android activity's own controls; the desktop has none.
}

void Perform(const storm::ui::Action &action)
{
    Log().error("not available on the desktop: the application's action {} {}", action.node, action.verb);
}

std::filesystem::path ModelsFolder()
{
    return storm::storage::folders().cache / "ui-models";
}

spdlog::sink_ptr LogSink(const char *)
{
    return nullptr;
}
} // namespace ui_platform
} // namespace storm::android::ui
#endif
