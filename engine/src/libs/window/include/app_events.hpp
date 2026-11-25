#pragma once

// Application window and lifecycle events: one subscription point for every subsystem. Handlers run on the game
// thread; the threading contract, early subscribers, Android's event order and the queue bounds: ../README.md.

#include <functional>

union SDL_Event;

namespace storm
{
enum class AppEventType
{
    WindowResized, // width/height: new drawable size in pixels
    WindowFocusGained,
    WindowFocusLost,
    WindowMinimized,
    WindowRestored,
    DisplayOrientationChanged, // orientation: SDL_DisplayOrientation, display: index
    WillEnterBackground,       // last chance to release surfaces / pause / save (early subscribers)
    DidEnterBackground,
    WillEnterForeground,
    DidEnterForeground, // surfaces may be recreated, audio may resume
    LowMemory,          // free caches
    Terminating,        // the OS is ending the process
};

struct AppEvent
{
    AppEventType type{};
    int width = 0;
    int height = 0;
    int orientation = 0;
    int display = 0;
    unsigned windowId = 0;
    // true while the event is being delivered inside the SDL call that raised
    // it (early subscribers only); false when delivered by dispatch().
    bool immediate = false;
};

class AppEvents
{
  public:
    using Handler = std::function<void(const AppEvent &)>;

    static AppEvents &instance();

    // Game thread, after SDL's event subsystem is initialized. Idempotent.
    void install();
    // Stops listening and drops queued events; subscriptions are kept.
    void uninstall();

    // Returns an id for unsubscribe(). Safe to call from handlers.
    int subscribe(Handler handler, bool early = false);
    void unsubscribe(int id);

    // Deliver queued events. Game thread only; call once per main-loop iteration.
    void dispatch();

    // Queue an event from any thread (platform bridges, tests). Early
    // subscribers are called immediately when this is the game thread.
    void post(const AppEvent &event);

    // SDL -> AppEvent mapping; false for events that are not application events.
    static bool translate(const SDL_Event &source, AppEvent &event);

  private:
    AppEvents();
    ~AppEvents();
    struct State;
    State *state_;
};
} // namespace storm
