package org.stormengine.port;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.view.MotionEvent;

import java.util.LinkedHashMap;
import java.util.Map;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Android host of the portable UI runtime for the application screens that run without the
 * engine (launcher, downloads, gallery; each in its own process): owns the surface, the
 * GLES 3 context and the frame pacing, forwards touches and states, and delivers component
 * actions on the UI thread. Documents, components, screens and drawing live in
 * libstorm_ui.so, drawn by bgfx on this view's GLES 3 context (the UI's one render backend,
 * as in the game). The game's UI is hosted by the engine itself ({@link EngineUi}).
 * The UI is the whole activity: every touch is its own.
 *
 * The layout viewport is the true size of the surface in dp (the display density, times the player's
 * «Размер интерфейса», GraphicsSettings.uiScale), so larger screens show more content instead of larger controls.
 */
final class StormUiView extends GLSurfaceView
        implements GLSurfaceView.Renderer, UiTextEditor.Answer {
    interface Listener {
        /** `command` is the engine queue line of a game interface action; empty for a plain document. */
        void onUiAction(String node, String verb, String value, String command);

        /** HUD input (the runtime's HUD presenter declarations): there is no HUD without the engine. */
        void onHudInput(String verb, String value, String slot, String command);

        /** A text field wants the system keyboard; answer with {@link #submitText}. Rectangle in view pixels. */
        void onUiTextRequest(
                String token,
                String text,
                String title,
                boolean numeric,
                boolean secret,
                int x,
                int y,
                int width,
                int height);

        void onUiFailed(String reason);
    }

    private static final String PACK = "ui";

    private final Listener listener;
    private final String gameRoot;
    private String document;

    /** GL thread only. */
    private long handle;

    // GL thread: what to restore after the context was lost.
    private final Map<String, byte[]> shownStates = new LinkedHashMap<>();

    /** GL thread: settles the UI and asks for a frame only when the shown one is out of date. */
    private final Runnable plan =
            new Runnable() {
                @Override
                public void run() {
                    plan();
                }
            };

    /** GL thread: a plan is queued and has not run yet. */
    private boolean planQueued;

    /** UI thread, for a timed change: plan on the GL thread. */
    private final Runnable later =
            new Runnable() {
                @Override
                public void run() {
                    queueEvent(plan);
                }
            };

    StormUiView(Context context, String document, String gameRoot, Listener listener) {
        super(context);
        this.document = document;
        this.gameRoot = gameRoot == null ? "" : gameRoot;
        this.listener = listener;
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 0, 8); // stencil: rounded clipping
        setPreserveEGLContextOnPause(true);
        setRenderer(this);
        setRenderMode(RENDERMODE_WHEN_DIRTY);
    }

    /**
     * Runs a native call on the GL thread and hands what it reported to the listener. Whether a
     * frame is needed is decided once for a whole burst of calls (a finger moving sends one
     * call per touch event): GLSurfaceView runs queued events before it draws, so settling
     * the UI after every single event would keep the GL thread busy and starve the frames of a
     * scroll.
     */
    private void call(final Runnable work) {
        queueEvent(
                new Runnable() {
                    @Override
                    public void run() {
                        work.run();
                        deliver();
                        if (!planQueued) {
                            planQueued = true;
                            queueEvent(plan);
                        } // after the events queued so far
                    }
                });
    }

    /** GL thread: settles the UI once, then asks for a frame only if the shown one is out of date. */
    private void plan() {
        planQueued = false;
        if (handle == 0) {
            requestRender();
            return;
        } // the host is made with the surface; that draws
        final double delay = StormUi.nativePrepare(handle);
        deliver();
        if (delay < 0) requestRender();
        else schedule(delay);
    }

    /** Any thread: plan again after `delay` seconds (nothing timed: never). */
    private void schedule(double delay) {
        removeCallbacks(later);
        if (delay < 3600) postDelayed(later, Math.max(1, (long) Math.ceil(delay * 1000)));
    }

    void loadDocument(final String path) {
        call(
                new Runnable() {
                    @Override
                    public void run() {
                        document = path;
                        if (handle != 0 && !StormUi.nativeLoadDocument(handle, path))
                            fail("Cannot load " + path);
                    }
                });
    }

    /** A state channel of the screen ("launcher", "downloads", …): JSON bytes. */
    void setState(final String channel, final byte[] json) {
        call(
                new Runnable() {
                    @Override
                    public void run() {
                        shownStates.put(channel, json);
                        if (handle != 0) StormUi.nativeSetState(handle, channel, json);
                    }
                });
    }

    @Override
    public void submitText(final String token, final String text, final boolean accepted) {
        call(
                new Runnable() {
                    @Override
                    public void run() {
                        if (handle != 0) StormUi.nativeSubmitText(handle, token, text, accepted);
                    }
                });
    }

    void toast(final String text, final String tone) {
        call(
                new Runnable() {
                    @Override
                    public void run() {
                        if (handle != 0) StormUi.nativeToast(handle, text, tone);
                    }
                });
    }

    /** Releases the native host on the GL thread; before {@link #onPause} it runs while the context is still current. */
    void release() {
        queueEvent(
                new Runnable() {
                    @Override
                    public void run() {
                        destroyHost();
                    }
                });
    }

    /**
     * One step of a pointer: phase {@link StormUi#DOWN} … {@link StormUi#CANCEL}.
     * Steps wait in a small queue that the GL thread drains in one event; a move replaces the
     * pointer's move still waiting (only where the finger is now matters), so a fast finger
     * costs one native call per pointer and frame, not one per input event, and no objects.
     */
    private void touch(int phase, int pointer, float x, float y, long time) {
        synchronized (touches) {
            touches.add(phase, pointer, x, y, time);
            if (touchesQueued) return;
            touchesQueued = true;
        }
        call(drainTouches);
    }

    private final PointerQueue touches = new PointerQueue(), drained = new PointerQueue();
    private boolean touchesQueued; // guarded by touches
    private final Runnable drainTouches =
            new Runnable() {
                @Override
                public void run() {
                    synchronized (touches) { // the GL thread works on its own copy
                        touchesQueued = false;
                        if (!touches.takeInto(drained, handle != 0))
                            return; // no runtime yet: kept for it (onSurfaceChanged)
                    }
                    for (int i = 0; i < drained.size; i++)
                        StormUi.nativeTouch(
                                handle,
                                drained.phase[i],
                                drained.pointer[i],
                                drained.x[i],
                                drained.y[i],
                                drained.time[i]);
                }
            };

    void back() {
        call(
                new Runnable() {
                    @Override
                    public void run() {
                        if (handle != 0) StormUi.nativeBack(handle);
                    }
                });
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        final int index = event.getActionIndex();
        final long time = event.getEventTime();
        if (action == MotionEvent.ACTION_MOVE) {
            // Every sample, oldest first (a move batches the ones since the last frame).
            final int history = event.getHistorySize();
            for (int h = 0; h < history; h++)
                for (int i = 0; i < event.getPointerCount(); i++)
                    touch(
                            StormUi.MOVE,
                            event.getPointerId(i),
                            event.getHistoricalX(i, h),
                            event.getHistoricalY(i, h),
                            event.getHistoricalEventTime(h));
            for (int i = 0; i < event.getPointerCount(); i++)
                touch(StormUi.MOVE, event.getPointerId(i), event.getX(i), event.getY(i), time);
        } else if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            touch(
                    StormUi.DOWN,
                    event.getPointerId(index),
                    event.getX(index),
                    event.getY(index),
                    time);
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            touch(
                    StormUi.UP,
                    event.getPointerId(index),
                    event.getX(index),
                    event.getY(index),
                    time);
        } else if (action == MotionEvent.ACTION_CANCEL) {
            for (int i = 0; i < event.getPointerCount(); i++)
                touch(StormUi.CANCEL, event.getPointerId(i), event.getX(i), event.getY(i), time);
        }
        return true;
    }

    // --- GL thread ---

    private void destroyHost() {
        if (handle != 0) StormUi.nativeDestroy(handle);
        handle = 0;
    }

    @Override
    public void onSurfaceCreated(GL10 unused, EGLConfig config) {
        // A new context: whatever the previous native host held on the GPU is gone with the old
        // one.
        destroyHost();
    }

    @Override
    public void onSurfaceChanged(GL10 unused, int newWidth, int newHeight) {
        final float density =
                getResources().getDisplayMetrics().density * GraphicsSettings.uiScale(getContext());
        if (handle == 0) {
            if (!StormUi.available()) {
                fail("This build has no UI runtime (libstorm_ui.so)");
                return;
            }
            handle =
                    StormUi.nativeCreate(
                            getContext().getAssets(),
                            PACK,
                            newWidth,
                            newHeight,
                            density,
                            gameRoot,
                            DebugBuild.on(getContext()));
            if (handle == 0) {
                fail("The UI runtime failed to start; see logcat tag StormUi");
                return;
            }
            if (document != null && !StormUi.nativeLoadDocument(handle, document))
                fail("Cannot load " + document);
            for (Map.Entry<String, byte[]> state : shownStates.entrySet())
                StormUi.nativeSetState(handle, state.getKey(), state.getValue());
            drainTouches
                    .run(); // touches made while the runtime started: it holds them for its first
            // frame
        } else {
            StormUi.nativeResize(handle, newWidth, newHeight, density);
        }
    }

    @Override
    public void onDrawFrame(GL10 unused) {
        if (handle == 0) return;
        final double delay = StormUi.nativeFrame(handle);
        deliver();
        // While something moves (a scroll, an animation) the next frame follows at once, at the
        // display's pace (Frame settles the UI itself); a timed change plans later.
        removeCallbacks(later);
        if (delay <= 0.001) requestRender();
        else schedule(delay);
    }

    /** Hands the actions and editor requests the runtime queued to the listener, on the UI thread. */
    private void deliver() {
        if (handle == 0 || listener == null) return;
        final String[] actions = StormUi.nativeTakeActions(handle);
        if (actions != null) {
            post(
                    new Runnable() {
                        @Override
                        public void run() {
                            for (int i = 0; i + 4 < actions.length; i += 5) {
                                if (actions[i].equals("hud"))
                                    listener.onHudInput(
                                            actions[i + 2],
                                            actions[i + 3],
                                            actions[i + 1],
                                            actions[i + 4]);
                                else
                                    listener.onUiAction(
                                            actions[i + 1],
                                            actions[i + 2],
                                            actions[i + 3],
                                            actions[i + 4]);
                            }
                        }
                    });
        }
        final String[] edit = StormUi.nativeTakeTextRequest(handle);
        if (edit != null) {
            post(
                    new Runnable() {
                        @Override
                        public void run() {
                            listener.onUiTextRequest(
                                    edit[0],
                                    edit[1],
                                    edit[2],
                                    edit[3].equals("1"),
                                    edit[8].equals("1"),
                                    (int) Float.parseFloat(edit[4]),
                                    (int) Float.parseFloat(edit[5]),
                                    (int) Float.parseFloat(edit[6]),
                                    (int) Float.parseFloat(edit[7]));
                        }
                    });
        }
    }

    private void fail(final String reason) {
        if (listener == null) return;
        post(
                new Runnable() {
                    @Override
                    public void run() {
                        listener.onUiFailed(reason);
                    }
                });
    }
}
