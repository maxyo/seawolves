package org.stormengine.port;

import android.graphics.Color;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.widget.*;

import java.util.ArrayList;

/** Thumb controls and touch-first menus, separate from the SDL render surface. */
final class MobileHud extends FrameLayout {
    private final GameActivity game;
    private final MenuTransition<MotionEvent> gate =
            new MenuTransition<>(
                    new MenuTransition.Host<MotionEvent>() {
                        @Override
                        public void startGap() {
                            game.releaseVirtualControls();
                            long now = SystemClock.uptimeMillis();
                            MotionEvent cancel =
                                    MotionEvent.obtain(
                                            now, now, MotionEvent.ACTION_CANCEL, 0, 0, 0);
                            try {
                                MobileHud.super.dispatchTouchEvent(cancel);
                            } finally {
                                cancel.recycle();
                            }
                            postDelayed(gapEnd, MenuTransition.GAP_MS);
                        }

                        @Override
                        public void stopGap() {
                            removeCallbacks(gapEnd);
                        }

                        @Override
                        public void apply(String screen) {
                            applyScreen();
                        }

                        @Override
                        public boolean dispatch(MotionEvent touch) {
                            return touches.dispatch(touch);
                        }

                        @Override
                        public MotionEvent copy(MotionEvent touch) {
                            return MotionEvent.obtain(touch);
                        }

                        @Override
                        public void recycle(MotionEvent touch) {
                            touch.recycle();
                        }
                    });
    private final Runnable gapEnd = gate::commit;

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
        if (gate.screen().isEmpty()
                && !loading
                && !gameplayState.land()
                && !gameplayState.sea()
                && !gameplayState.worldmap()) rebuild();
    }

    private final ArrayList<Runnable> gestureCancellers = new ArrayList<>();
    private boolean loading = true;
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
        gate.show(name);
    }

    /** The input went off: touches held for the menu that follows are not given to it. */
    void dropHeldTouches() {
        gate.dropHeld();
    }

    boolean menuTransitionPending() {
        return gate.pending();
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        // Every sample as the touch screen reports it, not batched to this window's next frame: the
        // game draws on its own thread and clock, and places each pointer for its own frame's time
        // from the samples (engine Host::PlaceTouches). Batched, the newest position it could see
        // would be up to a display frame old, by a varying amount.
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) requestUnbufferedDispatch(event);
        return gate.touch(event);
    }

    @Override
    protected void onDetachedFromWindow() {
        gate.detached();
        super.onDetachedFromWindow();
    }

    private void applyScreen() {
        game.releaseVirtualControls();
        removeAllViews();
        rebuild();
    }

    void refreshConfiguration() {
        rebuild();
    }

    private void rebuild() {
        if (gate.holdsScreen()) return;
        game.releaseVirtualControls();
        removeAllViews();
        setBackgroundColor(Color.TRANSPARENT);
        uiHud = false;
        // Leave the engine loading artwork visible, with no Android children over it.
        if (!gate.blocks()) {
            // An open interface is drawn by the portable UI; until its screen arrives the game
            // stays visible.
            if (gate.screen().isEmpty() && !loading) gameplay();
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
        if (!gate.readiness(next)) return;
        if (next.blocks()) {
            long now = SystemClock.uptimeMillis();
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
        if (!stays && gate.screen().isEmpty() && !loading && !cinematic && !dialogue) rebuild();
    }

    void cancelGestures() {
        for (Runnable reset : new ArrayList<>(gestureCancellers)) reset.run();
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
        // The portable UI's dialogue covers the game (the game opens no menu during a dialogue).
        if (dialogue) return;
        boolean modeless =
                !gameplayState.land() && !gameplayState.sea() && !gameplayState.worldmap();
        // A cut-scene, a blocked game, the death picture: the portable UI draws the one menu
        // button (HUD layout "menu"); nothing of this layout takes the screen. Otherwise the
        // portable UI draws the mode's HUD (or, while the engine blocks gameplay, what of it
        // still shows: the land sheet) and every other pointer arrives here and turns the camera.
        if (cinematic || (modeless && !uiHudShown)) return;
        uiHud = true;
        addLook(hudWords());
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
        look.setContentDescription(words.get("host.look"));
        final CameraGesture gesture =
                new CameraGesture(
                        new CameraGesture.Camera() {
                            public void lookStart(float x, float y, long time) {
                                GameActivity.traceInput("look start");
                                if (shown.worldmap())
                                    game.lookCamera(
                                            MobileMailbox.Message.lookDown(
                                                    x,
                                                    y,
                                                    time,
                                                    look.getWidth(),
                                                    look.getHeight(),
                                                    getResources().getDisplayMetrics().density));
                            }

                            public void look(float dx, float dy, float x, float y, long time) {
                                if (shown.worldmap())
                                    game.lookCamera(MobileMailbox.Message.lookMove(x, y, time));
                                else game.moveCamera(dx, dy);
                            }

                            public void lookEnd(boolean lifted, long time) {
                                GameActivity.traceInput("look end");
                                game.endCameraDrag();
                                if (shown.worldmap())
                                    game.lookCamera(
                                            lifted
                                                    ? MobileMailbox.Message.lookUp(time)
                                                    : MobileMailbox.Message.lookCancel());
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
        registerGesture(look, gesture::cancel);
        addView(look, 0, new LayoutParams(-1, -1));
    }

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);
        if (oldWidth > 0 && oldHeight > 0) game.releaseVirtualControls();
    }
}
