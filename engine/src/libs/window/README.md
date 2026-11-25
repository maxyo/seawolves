# Window and application events

`os_window.hpp` is the window abstraction (SDL2 implementation in `src/sdl_window.cpp`). `app_events.hpp` is the
engine-wide subscription point for window and application lifecycle events (renderer surface handling, UI layout,
audio, autosave, caches).

```cpp
const int id = storm::AppEvents::instance().subscribe([](const storm::AppEvent &e) {
    if (e.type == storm::AppEventType::WindowResized) resize(e.width, e.height);
});
// Early subscriber (second argument true): runs inside the SDL call that raised the event, before SDL gets
// control back, e.g. to release the surface on WillEnterBackground.
storm::AppEvents::instance().unsubscribe(id);
```

## Threading

Every handler runs on the game thread (the thread that called `install()`). SDL raises some events on other threads
(on Android focus, resize, low memory and terminate come from the Java UI thread); those are queued and delivered by
`dispatch()`, which the main loop calls once per iteration right after `SDL_PumpEvents()`, outside of any frame.

`subscribe(handler)` is delivered by `dispatch()`; use it by default. `subscribe(handler, true)` is an early
subscriber: when SDL raises the event on the game thread, the handler runs at once, inside the SDL call that raised
it; otherwise it runs at the start of the next `dispatch()`, before the normal subscribers. Each event reaches a
handler once.

## Android pause

SDL (2.0.18, `SDL_androidevents.c`) raises WindowMinimized, WillEnterBackground and DidEnterBackground on the game
thread from inside `SDL_PumpEvents()`, returns, and blocks that thread in a later `SDL_PumpEvents()` once
DidEnterBackground has left its queue. With the engine's loop (pump, `dispatch()`, flush) a normal subscriber still
runs before the thread is suspended, one `dispatch()` later than an early one. Use an early subscriber when the work
must be done before control returns to SDL at all (releasing a surface the OS is about to destroy) and keep it
quick: Android may kill a paused process. Early handlers must tolerate being called from inside an SDL function.

Event order on Android:

- Home or recents: WindowFocusLost, WillEnterBackground, DidEnterBackground, then after the suspension
  WillEnterForeground, DidEnterForeground, WindowFocusGained. The focus events come from the Java thread, so their
  place relative to the lifecycle pair varies.
- Rotation, fold, multi-window: WindowResized (the latest size only), DisplayOrientationChanged.
- onTrimMemory / onLowMemory: LowMemory (delivered on resume when suspended).
- Task removed: Terminating.

The queue is bounded: WindowResized keeps only the newest size, LowMemory is not duplicated, and when the queue is
full the oldest non-lifecycle event is dropped. Lifecycle events are never dropped.

## Scripts

Scripts receive `AppWillEnterBackground`, `AppDidEnterForeground`, `AppLowMemory` and
`AppWindowResized(width, height)` as ordinary script events (`apps/engine/src/main.cpp`).
`AppWillEnterBackground` handlers run synchronously on the game thread (the application's overlay autosaves
there); their time is logged.

Tests: the host checks `app_events.address` and `app_events.thread` (real host SDL, ASan/UBSan and TSan) and
`android_window` for the window mailbox.
