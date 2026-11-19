package org.stormengine.port;

import android.graphics.Color;
import android.view.MotionEvent;
import android.view.View;
import android.widget.*;

/** Thumb controls and touch-first menus, separate from the SDL render surface. */
final class MobileHud extends FrameLayout {
    private final GameActivity game;
    private String screen = "";
    private boolean emptyScreenPending;
    private final Runnable emptyScreenCommit = this::finishEmptyScreen;

    /** A dialogue is active; the portable UI draws it (s-dialogue). */
    private boolean dialogue;

    private boolean cinematic;
    private GameplayState gameplayState = GameplayState.UNKNOWN;

    /** The gameplay views mounted now are only the look surface under a HUD of the portable UI. */
    private boolean uiHud;

    /** The portable UI reports its HUD as shown (it can outlast the gameplay mode: an open land sheet). */
    private boolean uiHudShown;

    void showUiHud(boolean shown) {
        if (uiHudShown == shown) return;
        uiHudShown = shown;
        // Without a gameplay mode the look surface goes with the UI's HUD (an open land sheet).
        if (screen.isEmpty()
                && !loading
                && !gameplayState.land()
                && !gameplayState.sea()
                && !gameplayState.worldmap()) rebuild();
    }

    private final java.util.ArrayList<Runnable> gestureCancellers = new java.util.ArrayList<>();
    private boolean loading = true;
    private LocationReadiness.Snapshot readiness;
    private final UiTouchSplitter touches;

    MobileHud(GameActivity game, EngineUi ui) {
        super(game);
        this.game = game;
        // Pointers that go down on the portable UI are its own; the rest reach the views of this
        // layout.
        touches = new UiTouchSplitter(ui, event -> super.dispatchTouchEvent(event));
        setMotionEventSplittingEnabled(true);
        setClipChildren(false);
    }

    void showScreen(String name) {
        // Native scripts can close one menu a frame before opening the next.
        // Keep its opaque Android surface across that short empty-screen gap.
        if (name.isEmpty() && !screen.isEmpty()) {
            if (emptyScreenPending) return;
            emptyScreenPending = true;
            game.releaseVirtualControls();
            long now = android.os.SystemClock.uptimeMillis();
            MotionEvent cancel = MotionEvent.obtain(now, now, MotionEvent.ACTION_CANCEL, 0, 0, 0);
            try {
                super.dispatchTouchEvent(cancel);
            } finally {
                cancel.recycle();
            }
            postDelayed(emptyScreenCommit, 150);
            return;
        }
        removeCallbacks(emptyScreenCommit);
        emptyScreenPending = false;
        applyScreen(name);
        deliverHeldTouches(true);
    }

    private void finishEmptyScreen() {
        if (!emptyScreenPending) return;
        emptyScreenPending = false;
        applyScreen("");
        deliverHeldTouches(true);
    }

    /**
     * Touches made during the empty-screen gap wait for what comes after it (the next menu or
     * gameplay) and are then dispatched in order with their own times: the views of the menu that
     * is going get none of them, and none is lost.
     */
    private final java.util.ArrayList<MotionEvent> heldTouches = new java.util.ArrayList<>();

    private void deliverHeldTouches(boolean dispatch) {
        if (heldTouches.isEmpty()) return;
        java.util.ArrayList<MotionEvent> held = new java.util.ArrayList<>(heldTouches);
        heldTouches.clear();
        for (MotionEvent event : held)
            try {
                if (dispatch) touches.dispatch(event);
            } finally {
                event.recycle();
            }
    }

    boolean menuTransitionPending() {
        return emptyScreenPending;
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        // Every sample as the touch screen reports it, not batched to this window's next frame: the
        // game draws on its own thread and clock, and places each pointer for its own frame's time
        // from the samples (engine Host::PlaceTouches). Batched, the newest position it could see
        // would be up to a display frame old, by a varying amount.
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) requestUnbufferedDispatch(event);
        // Transparent input shield: the engine owns the stock loading artwork.
        if (readiness != null && readiness.blocks()) return true;
        if (emptyScreenPending) {
            heldTouches.add(MotionEvent.obtain(event));
            return true;
        }
        return touches.dispatch(event);
    }

    @Override
    protected void onDetachedFromWindow() {
        // Nothing is left to touch: what was held is given back to the pool, not dispatched.
        removeCallbacks(emptyScreenCommit);
        deliverHeldTouches(false);
        finishEmptyScreen();
        super.onDetachedFromWindow();
    }

    private void applyScreen(String name) {
        game.releaseVirtualControls();
        screen = name;
        removeAllViews();
        rebuild();
    }

    void refreshConfiguration() {
        rebuild();
    }

    private void rebuild() {
        if (emptyScreenPending && (readiness == null || !readiness.blocks())) return;
        game.releaseVirtualControls();
        removeAllViews();
        setBackgroundColor(Color.TRANSPARENT);
        uiHud = false;
        // Leave the engine loading artwork visible, with no Android children over it.
        if (readiness == null || !readiness.blocks()) {
            // An open interface is drawn by the portable UI; until its screen arrives the game
            // stays visible.
            if (screen.isEmpty() && !loading) gameplay();
        }
        game.publishPresentation();
    }

    /**
     * Android's part of the portable UI's state channel "presentation": whether the game is in front and
     * running. The engine adds its own facts (open interface, loading, cinematic, dialogue) in the frame
     * they change; they do not come back through this thread.
     */
    static String presentation(boolean live) {
        return live ? "{\"live\":true}" : "{\"live\":false}";
    }

    /** The dialogue itself (speech, replies, answer editor, menu button) is drawn by the portable UI; this layout only steps aside. */
    void showDialogue(boolean active) {
        if (dialogue == active) return;
        dialogue = active;
        rebuild();
    }

    void showReadiness(LocationReadiness.Snapshot next) {
        LocationReadiness.Snapshot previous = readiness;
        readiness = next;
        if (previous != null
                && previous.generation == next.generation
                && previous.state.equals(next.state)
                && previous.active == next.active
                && previous.retryable == next.retryable) {
            // Only the producer's phase changed: nothing to do here.
            return;
        }
        if (next.blocks()) {
            removeCallbacks(emptyScreenCommit);
            emptyScreenPending = false;
            deliverHeldTouches(false);
            long now = android.os.SystemClock.uptimeMillis();
            MotionEvent cancel = MotionEvent.obtain(now, now, MotionEvent.ACTION_CANCEL, 0, 0, 0);
            try {
                touches.cancelOwners(cancel);
            } finally {
                cancel.recycle();
            }
            cancelGestures();
            game.releaseVirtualControls();
        }
        rebuild();
    }

    void showLoading(boolean active) {
        if (loading != active) {
            loading = active;
            rebuild();
        }
    }

    void showCinematic(boolean active) {
        if (cinematic != active) {
            cinematic = active;
            rebuild();
        }
    }

    void showGameplayState(GameplayState next) {
        // A gameplay mode (its HUD is the portable UI's) needs nothing rebuilt here while the HUD
        // stays in the UI.
        boolean stays = uiHud && (next.land() || next.sea() || next.worldmap());
        gameplayState = next;
        if (!stays && screen.isEmpty() && !loading && !cinematic && !dialogue) rebuild();
    }

    void cancelGestures() {
        for (Runnable reset : new java.util.ArrayList<>(gestureCancellers)) reset.run();
    }

    private void registerGesture(View control, Runnable reset) {
        control.addOnAttachStateChangeListener(
                new View.OnAttachStateChangeListener() {
                    public void onViewAttachedToWindow(View v) {
                        if (!gestureCancellers.contains(reset)) gestureCancellers.add(reset);
                    }

                    public void onViewDetachedFromWindow(View v) {
                        reset.run();
                        gestureCancellers.remove(reset);
                    }
                });
        if (control.isAttachedToWindow()) gestureCancellers.add(reset);
    }

    MenuText hudWords() {
        return new MenuText(getContext(), gameplayState.language);
    }

    private void gameplay() {
        MenuText words = hudWords();
        // The portable UI's dialogue covers the game (the game opens no menu during a dialogue).
        if (dialogue) return;
        boolean pause =
                cinematic
                        || (!gameplayState.land()
                                && !gameplayState.sea()
                                && !gameplayState.worldmap());
        boolean modeless =
                !gameplayState.land() && !gameplayState.sea() && !gameplayState.worldmap();
        if (modeless && uiHudShown && !cinematic && !dialogue) {
            // The engine blocks gameplay while the UI's HUD still shows something of it (the land
            // sheet).
            uiHud = true;
            addLook(words);
            return;
        }
        if (!pause) {
            // The portable UI draws this mode's HUD and takes the pointers that land on it.
            // Every other pointer arrives here and turns the camera.
            uiHud = true;
            addLook(words);
            return;
        }
        // A cut-scene, a blocked game, the death picture: the portable UI draws the one menu
        // button (HUD layout "menu"); nothing of this layout takes the screen.
    }

    /**
     * The camera-look surface under the HUD of the portable UI: the whole free screen turns the
     * camera with one finger and pinches its zoom with two (CameraGesture); nothing is drawn
     * here. On the world map the finger moves the map's camera itself (engine
     * platform/android_camera_look.hpp: the camera that follows the ship turns, the free camera
     * pans; 1:1, coasting after a release); elsewhere it moves the engine's mouse, as the PC
     * game's camera look.
     */
    private void addLook(MenuText words) {
        final GameplayState shown = gameplayState;
        View look = new View(getContext());
        look.setContentDescription(words.get("look"));
        final CameraGesture gesture =
                new CameraGesture(
                        new CameraGesture.Camera() {
                            public void lookStart(float x, float y, long time) {
                                GameActivity.traceInput("look start");
                                if (shown.worldmap())
                                    game.lookCamera(
                                            "down\t"
                                                    + x
                                                    + "\t"
                                                    + y
                                                    + "\t"
                                                    + time
                                                    + "\t"
                                                    + look.getWidth()
                                                    + "\t"
                                                    + look.getHeight()
                                                    + "\t"
                                                    + getResources().getDisplayMetrics().density);
                            }

                            public void look(float dx, float dy, float x, float y, long time) {
                                if (shown.worldmap())
                                    game.lookCamera("move\t" + x + "\t" + y + "\t" + time);
                                else game.moveCamera(dx, dy);
                            }

                            public void lookEnd(boolean lifted, long time) {
                                GameActivity.traceInput("look end");
                                game.endCameraDrag();
                                if (shown.worldmap())
                                    game.lookCamera(lifted ? "up\t" + time : "cancel");
                            }

                            public void zoom(float scale) {
                                game.zoomCamera(scale);
                            }
                        });
        look.setOnTouchListener(
                (v, e) -> {
                    final boolean cancelled = e.getActionMasked() == MotionEvent.ACTION_CANCEL;
                    gesture.onTouch(e);
                    if (cancelled) game.releaseVirtualControls();
                    // Consume blank touches without turning them into native mouse clicks.
                    return true;
                });
        NauticalStyle.role(look, "look");
        registerGesture(look, gesture::cancel);
        addView(look, 0, new LayoutParams(-1, -1));
    }

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);
        if (oldWidth > 0 && oldHeight > 0) game.releaseVirtualControls();
    }
}
