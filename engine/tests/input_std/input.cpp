#define SDL_MAIN_HANDLED
#include "sdl_input.hpp"
#include "platform/host_hooks.hpp"
#include <SDL2/SDL.h>
#include <thread>
#include <vector>

#include <catch2/catch.hpp>

static void pushKey(Uint32 type, SDL_Scancode code, bool repeat = false)
{
    SDL_Event e{};
    e.type = type;
    e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = code;
    e.key.repeat = repeat;
    REQUIRE(SDL_PushEvent(&e) == 1);
}
#ifdef __ANDROID__
static void pushMouse(Uint32 type, Uint8 button)
{
    SDL_Event e{};
    e.type = type;
    e.button.button = button;
    REQUIRE(SDL_PushEvent(&e) == 1);
}
static void pushBoundary(Uint32 type, Uint8 windowEvent = 0)
{
    SDL_Event e{};
    e.type = type;
    e.window.event = windowEvent;
    REQUIRE(SDL_PushEvent(&e) == 1);
}
static void checkFrameButtons(storm::SDLInput &input)
{
    // The real adapter receives a press/release between updates, on the UI thread.
    std::thread ui([&] {
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
        pushKey(SDL_KEYUP, SDL_SCANCODE_E);
        pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
        pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT);
    });
    ui.join();
    input.PumpEvents();
    REQUIRE(input.KeyboardKeyState('E'));
    REQUIRE(input.KeyboardSDLKeyState(SDL_SCANCODE_E));
    REQUIRE(input.MouseKeyState(storm::MouseKey::Right));
    // Reads do not consume the edge: every control sharing a key sees one frame.
    REQUIRE(input.KeyboardKeyState('E'));
    input.PumpEvents();
    REQUIRE(!input.KeyboardKeyState('E'));
    REQUIRE(!input.MouseKeyState(storm::MouseKey::Right));

    // Independent held keys/buttons survive unrelated short presses.
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_W);
    pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
    input.PumpEvents();
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
    pushKey(SDL_KEYUP, SDL_SCANCODE_E);
    input.PumpEvents();
    REQUIRE((input.KeyboardKeyState('W') && input.KeyboardKeyState('E')));
    REQUIRE(input.MouseKeyState(storm::MouseKey::Left));
    input.PumpEvents();
    REQUIRE((input.KeyboardKeyState('W') && !input.KeyboardKeyState('E')));
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_W, true);
    pushKey(SDL_KEYUP, SDL_SCANCODE_W);
    pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_LEFT);
    input.PumpEvents();
    REQUIRE(!input.KeyboardKeyState('W'));
    REQUIRE(!input.MouseKeyState(storm::MouseKey::Left));

    // Cancel after releases discards an unconsumed tap, but permits a new press.
    int callbacks = 0;
    const auto subscription = input.Subscribe([&](const storm::InputEvent &) { ++callbacks; });
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
    pushKey(SDL_KEYUP, SDL_SCANCODE_E);
    pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
    pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT);
    storm::SDLInput::CancelPendingInput();
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_A);
    input.PumpEvents();
    REQUIRE(!input.KeyboardKeyState('E'));
    REQUIRE(!input.MouseKeyState(storm::MouseKey::Right));
    REQUIRE(input.KeyboardKeyState('A'));
    REQUIRE(callbacks == 1); // Only the fresh A press reaches the consumer.
    input.Unsubscribe(subscription);
    storm::SDLInput::CancelPendingInput();
    input.PumpEvents();
    REQUIRE(!input.KeyboardKeyState('A'));
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_A, true); // OS repeat cannot resurrect cancelled hold.
    input.PumpEvents();
    REQUIRE(!input.KeyboardKeyState('A'));

    for (bool app : {false, true})
    {
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
        pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
        pushBoundary(app ? SDL_APP_WILLENTERBACKGROUND : SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_LOST);
        input.PumpEvents();
        REQUIRE(!input.KeyboardKeyState('E'));
        REQUIRE(!input.MouseKeyState(storm::MouseKey::Right));
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_W); // Events while backgrounded are ignored.
        input.PumpEvents();
        REQUIRE(!input.KeyboardKeyState('W'));
        pushBoundary(app ? SDL_APP_DIDENTERFOREGROUND : SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_GAINED);
        input.PumpEvents();
        REQUIRE((!input.KeyboardKeyState('E') && !input.KeyboardKeyState('W')));
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
        pushKey(SDL_KEYUP, SDL_SCANCODE_E);
        input.PumpEvents();
        REQUIRE(input.KeyboardKeyState('E'));
        input.PumpEvents();
        REQUIRE(!input.KeyboardKeyState('E'));
    }

    // Window focus does not resume input while the app is still backgrounded.
    pushBoundary(SDL_APP_WILLENTERBACKGROUND);
    pushBoundary(SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_LOST);
    pushBoundary(SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_GAINED);
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
    input.PumpEvents();
    REQUIRE(!input.KeyboardKeyState('E'));
    pushBoundary(SDL_APP_DIDENTERFOREGROUND);
    input.PumpEvents();
    REQUIRE(!input.KeyboardKeyState('E'));
}
#endif
static void pushMotion(int dx, int dy)
{
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.xrel = dx;
    e.motion.yrel = dy;
    REQUIRE(SDL_PushEvent(&e) == 1);
}
#ifdef __ANDROID__
static void checkCancellationDuringPump(storm::SDLInput &input)
{
    for (int boundary = 0; boundary < 3; ++boundary)
    {
        bool cancelled = false;
        int staleKeyCallbacks = 0;
        const auto subscription = input.Subscribe([&](const storm::InputEvent &event) {
            if (event.type == storm::InputEvent::KeyboardKeyDown && std::get<storm::KeyboardKey>(event.data) == 'E')
                ++staleKeyCallbacks;
            if (event.type != storm::InputEvent::MouseMove || cancelled)
                return;
            REQUIRE(input.KeyboardKeyState('W'));
            REQUIRE(input.MouseKeyState(storm::MouseKey::Left));
            // A real producer cancels while PumpEvents owns a detached batch.
            // Joining establishes that the boundary completed before the reads.
            std::thread producer([&] {
                if (boundary == 0)
                    storm::SDLInput::CancelPendingInput();
                else if (boundary == 1)
                    pushBoundary(SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_LOST);
                else
                    pushBoundary(SDL_APP_WILLENTERBACKGROUND);
                pushKey(SDL_KEYDOWN, SDL_SCANCODE_A); // Belongs to the next batch.
            });
            producer.join();
            cancelled = true;
            REQUIRE(!input.KeyboardKeyState('W'));
            REQUIRE(!input.KeyboardSDLKeyState(SDL_SCANCODE_W));
            REQUIRE(!input.MouseKeyState(storm::MouseKey::Left));
        });
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_W);
        pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_LEFT);
        pushMotion(1, 0);
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
        pushKey(SDL_KEYUP, SDL_SCANCODE_E);
        pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
        pushMouse(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT);
        input.PumpEvents();
        REQUIRE((cancelled && staleKeyCallbacks == 0));
        REQUIRE((!input.KeyboardKeyState('W') && !input.KeyboardKeyState('E')));
        REQUIRE(!input.KeyboardKeyState('A'));
        REQUIRE(!input.MouseKeyState(storm::MouseKey::Left));
        REQUIRE(!input.MouseKeyState(storm::MouseKey::Right));
        input.Unsubscribe(subscription);
        input.PumpEvents();
        REQUIRE(input.KeyboardKeyState('A') == (boundary == 0));
        if (boundary == 1)
            pushBoundary(SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_GAINED);
        if (boundary == 2)
            pushBoundary(SDL_APP_DIDENTERFOREGROUND);
        storm::SDLInput::CancelPendingInput();
        input.PumpEvents();
    }

    // A completed cancellation also invalidates an already published frame,
    // without requiring another PumpEvents call or touching its button arrays.
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E);
    pushMouse(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT);
    input.PumpEvents();
    REQUIRE((input.KeyboardKeyState('E') && input.MouseKeyState(storm::MouseKey::Right)));
    std::thread producer([] { storm::SDLInput::CancelPendingInput(); });
    producer.join();
    REQUIRE(!input.KeyboardKeyState('E'));
    REQUIRE(!input.KeyboardSDLKeyState(SDL_SCANCODE_E));
    REQUIRE(!input.MouseKeyState(storm::MouseKey::Right));
    input.PumpEvents();
}
#endif

static void checkStockLoadingInput(storm::SDLInput &input)
{
    int delivered = 0;
    const int id = input.Subscribe([&](const storm::InputEvent &) { ++delivered; });
#ifdef __ANDROID__
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_E); // queued before transaction, must not replay
#endif
    {
        storm::platform::LoadingPlatformTransaction loading;
        storm::platform::CancelLoadingPlatformInput();
        std::thread delayedProducer([] {
            pushKey(SDL_KEYDOWN, SDL_SCANCODE_W);
            pushKey(SDL_KEYUP, SDL_SCANCODE_W);
            SDL_Event axis{};
            axis.type = SDL_CONTROLLERAXISMOTION;
            SDL_PushEvent(&axis);
        });
        delayedProducer.join();
        // No Controls update is run by the transaction pump.
        REQUIRE(delivered == 0);
#ifdef __ANDROID__
        pushBoundary(SDL_APP_WILLENTERBACKGROUND);
        pushBoundary(SDL_APP_DIDENTERFOREGROUND);
#endif
    }
    input.PumpEvents();
    REQUIRE(delivered == 0);
    REQUIRE((!input.KeyboardKeyState('E') && !input.KeyboardKeyState('W')));
    pushKey(SDL_KEYDOWN, SDL_SCANCODE_B);
    pushKey(SDL_KEYUP, SDL_SCANCODE_B);
    input.PumpEvents();
    REQUIRE(delivered == 2); // fresh input after loading still works
    input.Unsubscribe(id);
}

TEST_CASE("input_std.input")
{
    SDL_SetMainReady();
    REQUIRE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    SDL_Window *window = SDL_CreateWindow("input adapter integration", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          640, 480, SDL_WINDOW_SHOWN);
    REQUIRE(window);
    SDL_RaiseWindow(window);
    SDL_PumpEvents();
    {
        storm::SDLInput input;
        REQUIRE(input.SetRelativeMouseMode(true));
        REQUIRE(SDL_GetRelativeMouseMode() == SDL_TRUE);
        std::vector<storm::InputEvent> received;
        const auto consumer = std::this_thread::get_id();
        int sub = input.Subscribe([&](const storm::InputEvent &e) {
            REQUIRE(std::this_thread::get_id() == consumer);
            received.push_back(e);
        });
        pushKey(SDL_KEYDOWN, SDL_SCANCODE_W);
        pushMotion(18, -9);
        pushKey(SDL_KEYUP, SDL_SCANCODE_W);
#ifdef __ANDROID__
        // Compile the actual Android deferral branch against real host SDL.
        // This checks thread handoff semantics, not Android/JNI runtime itself.
        REQUIRE(received.empty());
#endif
        input.PumpEvents();
        REQUIRE(received.size() == 3);
        REQUIRE(received[0].type == storm::InputEvent::KeyboardKeyDown);
        REQUIRE(std::get<storm::KeyboardKey>(received[0].data) == 'W');
        REQUIRE(received[1].type == storm::InputEvent::MouseMove);
        auto delta = std::get<storm::MousePos>(received[1].data);
        REQUIRE((delta.x == 18 && delta.y == -9));
        REQUIRE(received[2].type == storm::InputEvent::KeyboardKeyUp);
        REQUIRE(std::get<storm::KeyboardKey>(received[2].data) == 'W');
#ifdef __ANDROID__
        REQUIRE(input.KeyboardKeyState('W')); // Short press remains active for this frame.
#endif
        received.clear();
        input.PumpEvents();
        REQUIRE(received.empty());             // A delta must not replay next frame.
        REQUIRE(!input.KeyboardKeyState('W')); // No press replay in the next frame.
#ifdef __ANDROID__
        std::thread ui([&] {
            pushMotion(123, 45); // Unconsumed camera motion before focus loss.
            pushKey(SDL_KEYDOWN, SDL_SCANCODE_A);
            pushKey(SDL_KEYUP, SDL_SCANCODE_A);
        });
        ui.join();
        REQUIRE(received.empty());
#endif
        REQUIRE(input.SetRelativeMouseMode(false));
        REQUIRE(SDL_GetRelativeMouseMode() == SDL_FALSE);
#ifdef __ANDROID__
        input.PumpEvents();
        REQUIRE(received.empty()); // Focus loss must not turn the cancelled tap into activation.
        REQUIRE(!input.KeyboardKeyState('A'));
        received.clear();
        pushMotion(999, -999); // Motion arriving while the app is paused.
        pushKey(SDL_KEYUP, SDL_SCANCODE_LSHIFT);
#endif
        REQUIRE(input.SetRelativeMouseMode(true));
        REQUIRE(SDL_GetRelativeMouseMode() == SDL_TRUE);
#ifdef __ANDROID__
        input.PumpEvents();
        REQUIRE(received.empty());
        received.clear();
#endif
        pushMotion(-6, 4); // New relative touch/physical input after resume.
        input.PumpEvents();
        REQUIRE(received.size() == 1);
        delta = std::get<storm::MousePos>(received[0].data);
        REQUIRE((delta.x == -6 && delta.y == 4));
        input.Unsubscribe(sub);
        checkStockLoadingInput(input);
#ifdef __ANDROID__
        checkFrameButtons(input);
        checkCancellationDuringPump(input);
#endif
    }
    REQUIRE(SDL_GetRelativeMouseMode() == SDL_FALSE); // Service teardown releases capture.
    SDL_DestroyWindow(window);
    SDL_Quit();
}
