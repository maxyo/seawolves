package org.stormengine.port;

import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.RelativeLayout;

import org.json.JSONException;
import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;

/** Native engine with a thumb-oriented Android interface. */
public final class GameActivity extends SDLActivity {
    /** The engine's thread runs in this process. */
    static boolean engineThreadAlive() {
        Thread thread = mSDLThread;
        return thread != null && thread.isAlive();
    }

    /** The engine runs: in this process, or the :game process (which ends with its engine) is listed. */
    static boolean isEngineAlive(Context context) {
        if (engineThreadAlive()) return true;
        List<ActivityManager.RunningAppProcessInfo> running =
                context.getSystemService(ActivityManager.class).getRunningAppProcesses();
        String game = context.getPackageName() + ":game";
        if (running != null)
            for (ActivityManager.RunningAppProcessInfo process : running)
                if (process.processName.equals(game) && process.pid != android.os.Process.myPid())
                    return true;
        return false;
    }

    private final MobileMailbox actions = new MobileMailbox();
    private final GameplayShortcuts shortcuts = new GameplayShortcuts();
    private volatile boolean resumed, windowFocused;
    private boolean destroying;
    private final BackDispatch back = new BackDispatch();
    private float fontScale;
    private int densityDpi;
    private GamePerformance performance;
    private MobileHud hud;

    /** The portable UI, hosted by the engine inside its frame. */
    private final EngineUi ui = new EngineUi();

    private UiTextEditor uiEditor;
    private String presentation = "";
    private boolean uiFailureShown;
    private String screen = "";
    private GameplayState gameplayState = GameplayState.UNKNOWN;

    private static native void nativeCancelInput();

    private AudioFocus audioFocus;

    public int getGraphicsSetting(String key, int fallback) {
        return GraphicsSettings.get(this, key, fallback);
    }

    /** The engine's automatic preset measurement chose `preset`, with the scene at `renderHeight` lines (> 0: its rung
     *  below «Низкая»; 0: the preset's own) (engine thread): stored unless the player chose one meanwhile, then the
     *  player is told once, in the game's `language`, and the engine re-reads the stored preferences. */
    public void autoPresetChosen(
            int preset, int renderHeight, int version, String report, String language) {
        boolean stored = GraphicsSettings.measured(this, preset, renderHeight, version, report);
        // «Низкая»'s speed for the launcher's minimum requirements check (MinimumRequirements),
        // also when the player's own preset kept the stored one.
        try {
            MinimumRequirements.recordLowFrame(getFilesDir(), report);
        } catch (IOException | JSONException error) {
            Log.e(
                    "StormLauncher",
                    "minimum requirements: cannot record «Низкая»'s frame time from the measurement "
                            + report,
                    error);
        }
        queueMobileAction(MobileMailbox.Message.GRAPHICS_RELOAD);
        runOnUiThread(
                () -> {
                    if (destroying) return;
                    ui.setState(
                            AppNames.CHANNEL_APP_SETTINGS,
                            GraphicsSettings.json(this).getBytes(StandardCharsets.UTF_8));
                    if (!stored) return;
                    MenuText words = new MenuText(this, language);
                    String name =
                            words.get(
                                    new String[] {
                                                "settings.fx_low",
                                                "settings.fx_medium",
                                                "settings.fx_high"
                                            }
                                            [
                                            GraphicsSettings.get(
                                                    this, AppNames.SETTING_FX_PRESET, 0)]);
                    ui.toast(
                            words.format(
                                    "host.graphics_preset_measured",
                                    PresetSource.measuredName(
                                            name,
                                            GraphicsSettings.get(
                                                    this, AppNames.FACT_FX_AUTO_RENDER_HEIGHT, 0))),
                            "info");
                });
    }

    /** The engine created its device on `backend` ("Vulkan", "OpenGL ES") for `reason` (engine thread, after every
     *  device creation): «О программе» shows it (GraphicsSettings.renderBackend). */
    public void renderBackendChosen(String backend, String reason) {
        GraphicsSettings.renderBackend(this, backend, reason);
    }

    public String getGameDataDirectory() {
        try {
            return GameStorage.readyPath(this);
        } catch (IOException e) {
            throw new IllegalStateException(e.getMessage(), e);
        }
    }

    /** The engine's one cache root (GameCache), read once by the engine at start. */
    public String getCacheRoot() {
        File external = getExternalCacheDir();
        File root = GameCache.root(external, getCacheDir());
        if (external == null)
            Log.w(
                    "StormCache",
                    "External cache storage is unavailable: the engine cache is in the internal one, "
                            + root);
        File marker = GameCache.clearMarker(getFilesDir());
        if (marker.exists()) {
            // «Очистить кэш» asked for it: emptied now, before the engine opens anything in the
            // root.
            try {
                for (GameCache.Removed removed : GameCache.remove(Collections.singletonList(root)))
                    Log.i("StormCache", "Cleared the engine cache as the player asked: " + removed);
            } catch (IOException error) {
                Log.e(
                        "StormCache",
                        "The engine cache could not be cleared fully; the engine rebuilds what is missing",
                        error);
            }
            if (!marker.delete())
                Log.e(
                        "StormCache",
                        "Cannot remove "
                                + marker
                                + ": the cache is cleared again at the next start");
        }
        if (!root.isDirectory() && !root.mkdirs())
            throw new IllegalStateException("Cannot create the engine cache " + root);
        // The layers the engine reads at start: a clear of the cache (the system's or the one
        // above) removed them.
        try {
            GameStorage.layers(this).install();
        } catch (Exception error) {
            throw new IllegalStateException("Cannot install the APK's layers into " + root, error);
        }
        ExitDiagnostics.writePreviousGameExit(
                this, root); // before the engine's GPU crash guards read it
        cacheRoot = root;
        measureCache(root);
        return root.getAbsolutePath();
    }

    private static native void nativeSurfaceDestroying();

    private static native void nativeSurfaceChanged();

    private static native void nativeDensityChanged();

    @Override
    protected String[] getLibraries() {
        return new String[] {"c++_shared", "SDL2", "engine"};
    }

    @Override
    protected void onCreate(Bundle state) {
        fontScale = getResources().getConfiguration().fontScale;
        densityDpi = getResources().getConfiguration().densityDpi;
        super.onCreate(state);
        // One game Activity per process (see endProcess): a second one can only be created in the
        // moments before a finished game's process ends (Play tapped while it closes). It cannot
        // run the engine again here, so the process ends now and the next Play starts afresh.
        if (activityCreated) {
            endProcess(
                    "A game Activity was started in a game process that is ending; start the game again");
            return;
        }
        activityCreated = true;
        // An import may have started after the launcher queued Play.
        // SDL has initialized here, but its native thread has not resumed yet.
        if (ResourceImporter.importing(GameStorage.imported(this))) {
            finish();
            return;
        }
        // A full storage: the engine would fail on its cache root, logs and saves. The launcher
        // says how much to free.
        File cacheVolume = getExternalCacheDir() != null ? getExternalCacheDir() : getCacheDir();
        long shortfall =
                GameCache.startShortfall(
                        getFilesDir().getUsableSpace(), cacheVolume.getUsableSpace());
        if (shortfall > 0) {
            Log.e(
                    "StormCache",
                    "Not enough free storage to start the game: free "
                            + shortfall
                            + " more bytes (files "
                            + getFilesDir().getUsableSpace()
                            + ", cache "
                            + cacheVolume.getUsableSpace()
                            + " usable)");
            startActivity(
                    new Intent(this, GameLauncherActivity.class)
                            .putExtra(GameLauncherActivity.EXTRA_START_NO_SPACE, shortfall)
                            .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
            super.finish();
            endProcess("The game was not started: not enough free storage");
            return;
        }
        if (mLayout == null) return;
        // The renderer owns the EGL surface on SDL's SurfaceView. Before SDL lets Android destroy
        // that surface, wait until the game thread has stopped rendering to it. Callbacks run in
        // registration order, so this one is placed ahead of SDL's own.
        if (mSurface != null) {
            // SDLSurface is package-private: reach it through its public base types.
            final SurfaceHolder holder = ((SurfaceView) (View) mSurface).getHolder();
            final SurfaceHolder.Callback sdlCallback = (SurfaceHolder.Callback) (Object) mSurface;
            holder.removeCallback(sdlCallback);
            holder.addCallback(
                    new SurfaceHolder.Callback() {
                        @Override
                        public void surfaceCreated(SurfaceHolder h) {}

                        @Override
                        public void surfaceChanged(
                                SurfaceHolder h, int format, int width, int height) {
                            nativeSurfaceChanged();
                        }

                        @Override
                        public void surfaceDestroyed(SurfaceHolder h) {
                            nativeSurfaceDestroying();
                        }
                    });
            holder.addCallback(sdlCallback);
        }
        // Android 13+ with enableOnBackInvokedCallback, and every Android 16 device for
        // targetSdk 36, no longer delivers onBackPressed/KEYCODE_BACK. The game always
        // consumes Back (menu/modal navigation), so the callback stays registered.
        BackNavigation.register(this, this::onBackPressed);
        performance = new GamePerformance(this);
        // The portable UI is drawn by the engine inside its frame; the Java HUD above routes the
        // pointers.
        ui.setState(
                AppNames.CHANNEL_APP_SETTINGS,
                GraphicsSettings.json(this).getBytes(StandardCharsets.UTF_8));
        uiEditor = new UiTextEditor(mLayout, ui);
        audioFocus =
                new AudioFocus(
                        this, focus -> queueMobileAction(MobileMailbox.Message.audioFocus(focus)));
        hud = new MobileHud(this, ui);
        mLayout.addView(hud, new RelativeLayout.LayoutParams(-1, -1));
        hud.showScreen(screen);
        mLayout.setOnApplyWindowInsetsListener(
                (view, insets) -> {
                    int left = insets.getSystemWindowInsetLeft(),
                            right = insets.getSystemWindowInsetRight();
                    int top = insets.getSystemWindowInsetTop(),
                            bottom = insets.getSystemWindowInsetBottom();
                    if (Build.VERSION.SDK_INT >= 30) {
                        Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
                        left = bars.left;
                        right = bars.right;
                        top = bars.top;
                        bottom = bars.bottom;
                    }
                    // The display cutout (camera hole, notch) is ignored: the UI runs under it,
                    // nothing moves for it.
                    hud.setPadding(left, top, right, bottom);
                    ui.setSafeArea(left, top, right, bottom);
                    return insets;
                });
        immersive();
        // The scenario driver of a --test-loop debug build (a no-op otherwise).
        TestLoop.game(this, ui, mSurface == null ? null : (SurfaceView) (View) mSurface);
    }

    /**
     * SDL recomputes the requested orientation from the native window (portrait when it
     * is not wider than tall, any orientation when resizable) and would override the
     * manifest. The game is landscape-only on every path.
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        boolean fontScaleChanged = Float.compare(fontScale, configuration.fontScale) != 0;
        fontScale = configuration.fontScale;
        // Display density changes (wm density, foldables, display-size settings) are handled here
        // as well:
        // a relaunch would destroy SDL's activity under the running engine.
        boolean densityChanged = densityDpi != configuration.densityDpi;
        densityDpi = configuration.densityDpi;
        if (densityChanged && !destroying && !isDestroyed() && !mBrokenLibraries)
            nativeDensityChanged();
        // Rebuild text and controls at the new scale without restarting the native engine.
        if ((fontScaleChanged || densityChanged)
                && !destroying
                && !isDestroyed()
                && !mBrokenLibraries
                && mLayout != null
                && hud != null) {
            releaseVirtualControls();
            hud.refreshConfiguration();
        }
    }

    private void immersive() {
        // Draw under the cutout on every edge, full screen; the UI ignores it.
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode =
                    Build.VERSION.SDK_INT >= 30
                            ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                            : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attributes);
        }
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(
                        WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else
            getWindow()
                    .getDecorView()
                    .setSystemUiVisibility(
                            View.SYSTEM_UI_FLAG_FULLSCREEN
                                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        windowFocused = focus;
        if (!focus) back.cancel();
        if (!focus && mLayout != null && !mBrokenLibraries) releaseVirtualControls();
        super.onWindowFocusChanged(focus);
        publishPresentation();
        // A permanent focus loss (another player took over) is never followed by a
        // gain callback; ask again when the player is back in the game window.
        if (focus && resumed && audioFocus != null) audioFocus.acquire();
        if (focus) immersive();
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        nativeLifecycle(false);
        publishPresentation();
        if (audioFocus != null) audioFocus.acquire();
    }

    @Override
    protected void resumeNativeThread() {
        if (ResourceImporter.importing(GameStorage.imported(this)) && !isEngineAlive(this)) {
            finish();
            return;
        }
        // The system's «Очистить кэш» may have removed the APK's layers while the game was in the
        // background; the engine reads them at the same paths, so they are back before it runs
        // another
        // frame.
        try {
            if (cacheRoot != null) GameStorage.layers(this).install();
        } catch (Exception error) {
            throw new IllegalStateException("Cannot install the APK's layers again", error);
        }
        if (!destroying && !isFinishing() && !isDestroyed()) super.resumeNativeThread();
    }

    @Override
    protected void onDestroy() {
        destroying = true;
        closeUiEditor();
        if (audioFocus != null) audioFocus.release();
        publishPresentation();
        resumed = false;
        stopEngine();
        super.onDestroy();
        endProcess("Game Activity destroyed: ending the game process");
    }

    private static boolean activityCreated;

    /**
     * The engine runs once per process (its native globals hold one run; the engine aborts a
     * second SDL_main in one process), while SDL would start SDL_main again in the same process
     * for the next game Activity. The :game process hosts only this Activity and ends with its
     * engine: when SDL_main returns (the game's own exit, see finish) or when the Activity is
     * destroyed (onDestroy, after stopEngine). The next start is a fresh process.
     */
    private static void endProcess(String reason) {
        Log.i("StormExit", reason);
        System.exit(0);
    }

    /**
     * SDL's engine thread calls finish() once SDL_main has returned. Android destroys a finishing
     * Activity only seconds later, when the launcher below is idle, and a Play tap meanwhile would
     * create the next game Activity in this process: the process therefore ends at once.
     */
    @Override
    public void finish() {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            super.finish();
            return;
        }
        if (exitToMainMenu) {
            // The pause menu's exit leads to the game's main menu: the launcher starts a fresh
            // engine there once this process is gone (GameRestart).
            startActivity(
                    new Intent(this, GameLauncherActivity.class)
                            .putExtra(
                                    GameLauncherActivity.EXTRA_MAIN_MENU_AFTER,
                                    android.os.Process.myPid())
                            .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
            super.finish();
            endProcess(
                    "Engine returned for the main menu: ending the game process, the launcher starts a fresh one");
        }
        super.finish();
        endProcess("Engine returned: ending the game process");
    }

    /** Set by the pause menu's «Выйти» (UI host action app-exit-to-main-menu) before the game's exit runs. */
    private volatile boolean exitToMainMenu;

    static final String EXTRA_MAIN_MENU = "org.stormengine.port.MAIN_MENU";

    /** Asked by the engine at start: skip the videos before the main menu (the game's own video break). */
    public boolean startsAtMainMenu() {
        return getIntent().getBooleanExtra(EXTRA_MAIN_MENU, false);
    }

    private static native boolean nativeSaveInProgress();

    /** The engine's session marker (engine log_rotation.hpp SessionMarker): true when the game goes to the background, false back in the foreground. */
    private static native void nativeLifecycle(boolean background);

    /**
     * Replaces SDL's unbounded join in onDestroy: sends the quit event and waits within EngineExit's bounds, then
     * kills the process (logged) rather than block the UI thread; a save cut at the cap keeps the previous file.
     */
    private void stopEngine() {
        Thread engine = mSDLThread;
        if (engine == null || mBrokenLibraries) return;
        long start = SystemClock.elapsedRealtime();
        SDLActivity.nativeSendQuit();
        boolean stopped;
        try {
            stopped =
                    EngineExit.await(
                            engine,
                            GameActivity::nativeSaveInProgress,
                            EngineExit.WAIT_MILLIS,
                            EngineExit.SAVE_WAIT_MILLIS);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            stopped = !engine.isAlive();
        }
        long waited = SystemClock.elapsedRealtime() - start;
        if (stopped) {
            Log.i("StormExit", "Engine stopped " + waited + " ms after the quit request");
            mSDLThread = null;
            return;
        }
        Log.e(
                "StormExit",
                "Engine did not stop "
                        + waited
                        + " ms after the quit request (inside a long frame such as a scene load"
                        + (nativeSaveInProgress()
                                ? ", with a save still being written: the previous save file is kept"
                                : "")
                        + "); killing the process instead of blocking the UI thread");
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (performance != null && !mBrokenLibraries) performance.start();
    }

    @Override
    protected void onStop() {
        if (performance != null) performance.stop();
        super.onStop();
    }

    /**
     * Forwards memory pressure to the engine as SDL_APP_LOWMEMORY (the event the engine
     * lifecycle consumes). SDL forwards only onLowMemory; Android 14+ mostly delivers
     * TRIM_MEMORY_UI_HIDDEN/BACKGROUND instead, so trimming on background is forwarded too.
     */
    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (mBrokenLibraries || mSDLThread == null) return;
        if (level >= TRIM_MEMORY_BACKGROUND
                || level == TRIM_MEMORY_RUNNING_LOW
                || level == TRIM_MEMORY_RUNNING_CRITICAL) {
            Log.i("StormMemory", "onTrimMemory " + level + " -> SDL_APP_LOWMEMORY");
            SDLActivity.nativeLowMemory();
        }
    }

    /** Called on the engine thread; all Android view work is posted to the UI thread. */
    public void showMobileScreen(String name) {
        runOnUiThread(
                () -> {
                    screen = name;
                    closeUiEditor();
                    TestLoop.screen(name);
                    dropQueuedActions();
                    shortcuts.clear();
                    if (hud != null) hud.showScreen(name);
                });
    }

    /** Native stock-loading pump: no shortcut or gameplay command dispatch. */
    public boolean pollLocationLoadCancel(long generation) {
        return actions.pollLocationLoadCancel(generation);
    }

    private static native void nativeShortcut(String name);

    private static native void nativeAudioFocus(boolean audible, boolean ducked);

    private static native void nativeGraphicsReload();

    private static native void nativeHudOwnerLost();

    private static native void nativeMailboxBack();

    private static native void nativeLocationLoadCancel(long generation);

    private static native void nativeCameraZoom(float scale);

    private static native void nativeLookDown(
            float x, float y, long time, float width, float height, float density);

    private static native void nativeLookMove(float x, float y, long time);

    private static native void nativeLookUp(long time);

    private static native void nativeLookCancel();

    private static final MobileMailbox.Engine ENGINE =
            new MobileMailbox.Engine() {
                public void audioFocus(boolean audible, boolean ducked) {
                    nativeAudioFocus(audible, ducked);
                }

                public void graphicsReload() {
                    nativeGraphicsReload();
                }

                public void hudOwnerLost() {
                    nativeHudOwnerLost();
                }

                public void back() {
                    nativeMailboxBack();
                }

                public void locationLoadCancel(long generation) {
                    nativeLocationLoadCancel(generation);
                }

                public void cameraZoom(float scale) {
                    nativeCameraZoom(scale);
                }

                public void lookDown(
                        float x, float y, long time, float width, float height, float density) {
                    nativeLookDown(x, y, time, width, height, density);
                }

                public void lookMove(float x, float y, long time) {
                    nativeLookMove(x, y, time);
                }

                public void lookUp(long time) {
                    nativeLookUp(time);
                }

                public void lookCancel() {
                    nativeLookCancel();
                }
            };

    /** Engine thread, once per frame: the shortcut and the mailbox's messages, each through its native. */
    public void deliverMobileActions() {
        final boolean ready = resumed && windowFocused;
        String shortcut = shortcuts.poll(SystemClock.uptimeMillis(), ready);
        if (shortcut != null) nativeShortcut(shortcut);
        for (MobileMailbox.Message message; (message = actions.poll()) != null; )
            message.deliver(ENGINE);
    }

    /** The system editor of a UI text field goes with the screen, the pause and the activity. */
    private void closeUiEditor() {
        uiEditor.close(false);
    }

    /** What the portable UI asks of Android (engine thread): pause, the shortcuts mailbox, the HUD presence. */
    public void onUiHudInput(String verb, String value) {
        runOnUiThread(() -> hudInput(verb, value));
    }

    /** Actions of the UI without an engine command: the application's settings and About. */
    public void onUiAction(String node, String verb, String value) {
        // On the engine thread, before the press that ends the engine is processed.
        if (node.equals(AppNames.HOST_APP_EXIT_TO_MAIN_MENU)) {
            exitToMainMenu = true;
            Log.i("StormExit", "Pause menu exit: the game restarts at its main menu");
            return;
        }
        runOnUiThread(
                () -> {
                    if (destroying) return;
                    if (node.equals(AppNames.HOST_APP_SETTINGS)) applyGraphicsSettings(value);
                    else if (node.equals(AppNames.HOST_APP_CACHE_CLEAR)) requestCacheClear();
                    else if (node.equals(
                            AppNames.HOST_APP_CACHE_MEASURE)) { // the settings screen opened
                        if (cacheRoot != null) measureCache(cacheRoot);
                        else
                            Log.e(
                                    "StormCache",
                                    "The settings screen asked for the cache size before the engine had its cache root");
                    }
                    // About is a launcher screen: the launcher (main process, its own UI host)
                    // comes to the front.
                    else if (node.equals(AppNames.HOST_APP_ABOUT))
                        startActivity(
                                new Intent(GameActivity.this, GameLauncherActivity.class)
                                        .putExtra(GameLauncherActivity.EXTRA_ABOUT, true)
                                        .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
                    else
                        reportInterfaceProblem("UI action without a handler: " + node + " " + verb);
                });
    }

    /** A text field wants the system keyboard; rectangle in surface pixels. The answer goes back through {@link EngineUi#submitText}. */
    public void onUiTextRequest(
            String token,
            String text,
            String title,
            boolean numeric,
            boolean secret,
            int x,
            int y,
            int width,
            int height) {
        runOnUiThread(
                () -> {
                    if (!destroying)
                        uiEditor.open(token, text, numeric, secret, x, y, width, height);
                });
    }

    /** An interface cannot be shown as designed: an error in the log, and a visible notice (the UI's toast, at the top) in debug builds. */
    void reportInterfaceProblem(String message) {
        Log.e("StormUI", message);
        if (DebugBuild.on(this)) ui.toast(message, "danger");
    }

    /** «Вибрация»: the fight moments of the engine's android::Haptic, which passes them to onHaptic. */
    static final int HAPTIC_HIT = 0, HAPTIC_BLOCK = 1, HAPTIC_BROADSIDE = 2, HAPTIC_BOARDING = 3;

    /** A fight moment (HAPTIC_*; the engine already checked the setting), played as the view's haptic feedback,
     *  which follows the system's touch-feedback setting; API 30's CONFIRM and REJECT where they exist. Engine thread. */
    public void onHaptic(int kind) {
        final boolean modern = Build.VERSION.SDK_INT >= 30;
        final int effect;
        switch (kind) {
            case HAPTIC_HIT:
                effect =
                        modern
                                ? HapticFeedbackConstants.REJECT
                                : HapticFeedbackConstants.LONG_PRESS;
                break;
            case HAPTIC_BLOCK:
                effect = HapticFeedbackConstants.CONTEXT_CLICK;
                break;
            case HAPTIC_BROADSIDE:
                effect = HapticFeedbackConstants.LONG_PRESS;
                break;
            case HAPTIC_BOARDING:
                effect =
                        modern
                                ? HapticFeedbackConstants.CONFIRM
                                : HapticFeedbackConstants.LONG_PRESS;
                break;
            default:
                Log.e("StormHaptics", "unknown haptic moment " + kind);
                return;
        }
        runOnUiThread(
                () -> {
                    if (destroying) return;
                    View view = getWindow().getDecorView();
                    if (!view.performHapticFeedback(effect))
                        Log.i(
                                "StormHaptics",
                                "haptic "
                                        + kind
                                        + " not played (the system's touch feedback is off or the device has none)");
                });
    }

    /** The engine's cache root (getCacheRoot), measured again whenever the settings screen opens. */
    private volatile File cacheRoot;

    /** The cache's size for the settings screen, measured off the engine's thread, then reported (appSettings). */
    private void measureCache(File root) {
        new Thread(
                        () -> {
                            long bytes;
                            try {
                                bytes = Trees.size(root);
                            } catch (IOException error) {
                                Log.e(
                                        "StormCache",
                                        "Cannot measure the engine cache " + root,
                                        error);
                                return;
                            }
                            GraphicsSettings.cacheMeasured(bytes);
                            Log.i(
                                    "StormCache",
                                    "engine cache: " + (bytes >> 20) + " MiB in " + root);
                            runOnUiThread(
                                    () -> {
                                        if (!destroying)
                                            ui.setState(
                                                    AppNames.CHANNEL_APP_SETTINGS,
                                                    GraphicsSettings.json(this)
                                                            .getBytes(StandardCharsets.UTF_8));
                                    });
                        },
                        "StormCacheSize")
                .start();
    }

    /** «Очистить кэш»: the marker for the next start; the settings screen shows it as pending. */
    private void requestCacheClear() {
        File marker = GameCache.clearMarker(getFilesDir());
        try {
            if (!marker.exists() && !marker.createNewFile()) throw new IOException("not created");
        } catch (IOException error) {
            settingsFailed(
                    "cache_clear_not_marked",
                    "The cache cannot be marked for clearing: " + error.getMessage());
            return;
        }
        Log.i(
                "StormCache",
                "The player asked to clear the engine cache: emptied at the next start");
        ui.setState(
                AppNames.CHANNEL_APP_SETTINGS,
                GraphicsSettings.json(this).getBytes(StandardCharsets.UTF_8));
    }

    /** The settings screen sent the renderer preferences («Готово»): the graphics effects apply at once, the rest at the next start. */
    private void applyGraphicsSettings(String json) {
        try {
            GraphicsSettings.apply(this, json);
        } catch (JSONException malformed) {
            settingsFailed(
                    "settings_not_saved",
                    "Graphics settings were not saved: " + malformed.getMessage());
            return;
        }
        ui.setState(
                AppNames.CHANNEL_APP_SETTINGS,
                GraphicsSettings.json(this).getBytes(StandardCharsets.UTF_8));
        // The engine re-applies the graphics effects from the stored preferences at its next frame.
        queueMobileAction(MobileMailbox.Message.GRAPHICS_RELOAD);
    }

    /** A settings action failed (UI thread): logged, and the player is told in the UI's toast place, in the game's
     *  language; the interface and the controls stay as they are. */
    private void settingsFailed(String key, String reason) {
        Log.e("StormSettings", reason);
        ui.toast(new MenuText(this, gameplayState.language).get("host." + key), "danger");
    }

    /**
     * Upcall (game thread): an engine error that ends the game, at start-up or in play. Its dialog titled by `title` (a
     * key of the strings tables) in the game's language (MenuText), with the reason as
     * the engine wrote it; the game thread waits until the player dismisses it, as with SDL's dialog.
     */
    public void showEngineError(String title, String reason) throws InterruptedException {
        CountDownLatch dismissed = new CountDownLatch(1);
        runOnUiThread(
                () -> {
                    if (destroying || isFinishing() || isDestroyed()) { // no window to show it in
                        Log.e("StormEngine", "Game stopped while the activity closes: " + reason);
                        dismissed.countDown();
                        return;
                    }
                    MenuText words = new MenuText(this, gameplayState.language);
                    new AlertDialog.Builder(this)
                            .setTitle(words.get(title))
                            .setMessage(words.format("host.game_stopped_reason", reason))
                            .setCancelable(false)
                            .setPositiveButton(android.R.string.ok, null)
                            .setOnDismissListener(d -> dismissed.countDown())
                            .show();
                });
        dismissed.await();
    }

    /** The portable UI cannot draw: there is no other renderer for its screens, so the player is told. */
    void uiFailed(String reason) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            runOnUiThread(() -> uiFailed(reason));
            return;
        } // from the engine thread
        Log.e("StormUI", "UI runtime failure: " + reason);
        // The UI's stick and held controls last until its release events, which a failed UI
        // cannot send: let go of them explicitly (engine input cancellation).
        releaseVirtualControls();
        // The engine keeps native HUD parts off while the UI owes an answer: give them back now.
        queueMobileAction(MobileMailbox.Message.HUD_OWNER_LOST);
        if (uiFailureShown || destroying || isFinishing()) return;
        uiFailureShown = true;
        new AlertDialog.Builder(this)
                .setTitle(new MenuText(this, gameplayState.language).get("host.interface_error"))
                .setMessage(reason)
                .setPositiveButton(android.R.string.ok, null)
                .setOnDismissListener(d -> uiFailureShown = false)
                .show();
    }

    /**
     * Android's part of the portable UI's state channel "presentation": whether the player can act (the
     * game is in front and running). The UI draws its HUD only on a free, live game. The same fact is the
     * engine's input epoch: no touch sent before the input went off acts after it.
     */
    void publishPresentation() {
        if (hud == null) return;
        boolean live = resumed && windowFocused && !destroying;
        ui.setInputLive(live);
        if (!live) hud.dropHeldTouches();
        String next = MobileHud.presentation(live);
        if (next.equals(presentation)) return;
        presentation = next;
        ui.setState(AppNames.CHANNEL_PRESENTATION, next.getBytes(StandardCharsets.UTF_8));
    }

    /**
     * HUD input of the portable UI that only Android can do (its engine commands stay in the
     * engine): menu shortcuts through their expiring mailbox and the HUD's presence.
     */
    private void hudInput(String verb, String value) {
        if (destroying) return;
        if (verb.equals(AppNames.HUD_SHORTCUT)) requestGameplayShortcut(value);
        else if (verb.equals(AppNames.HUD_PRESENCE)) {
            if (hud != null) hud.showUiHud(value.equals(AppNames.PRESENCE_SHOWN));
        } else reportInterfaceProblem("HUD input without a handler: " + verb);
    }

    /** Called on the engine thread: a dialogue covers the game (the engine's UI host draws it from the engine's own state). */
    public void showDialogue(boolean active) {
        runOnUiThread(
                () -> {
                    if (!destroying && !isDestroyed() && hud != null) hud.showDialogue(active);
                });
    }

    /** Engine thread, for a new gameplay revision: the snapshot the activity's own controls follow. */
    public void showGameplayState(
            long revision,
            String mode,
            String language,
            String inputOwner,
            boolean analogMovement,
            long movementGeneration) {
        final GameplayState next =
                GameplayState.of(
                        revision, mode, language, inputOwner, analogMovement, movementGeneration);
        runOnUiThread(
                () -> {
                    if (destroying || isDestroyed() || gameplayState.same(next)) return;
                    // On land the same hero keeps the stick through a stance change; the portable
                    // UI ends its own combat holds with the combat state. Everything else lets go.
                    if (!(screen.isEmpty()
                            && resumed
                            && windowFocused
                            && gameplayState.sameLandInputOwner(next))) releaseVirtualControls();
                    gameplayState = next;
                    String name = next.mode.name().toLowerCase(Locale.ROOT);
                    traceInput("state " + name + " owner " + next.inputOwner);
                    TestLoop.gameplay(name);
                    Log.i("StormGameplay", "mode=" + name + " revision=" + next.revision);
                    if (hud != null) hud.showGameplayState(next);
                });
    }

    private final LocationReadiness locationReadiness = new LocationReadiness();
    private final Object loadingInputLock = new Object();

    /** Native calls synchronously before its ordinary quit route; menu destination must be recorded now. A state or
     *  answer the engine does not name throws: its call (CallActivity) logs it and stops the reveal. */
    public void onLocationReadiness(long generation, String state, boolean active, String ack) {
        final LocationReadiness.Snapshot next;
        synchronized (loadingInputLock) {
            next =
                    locationReadiness.accept(
                            generation,
                            LocationReadiness.state(state),
                            active,
                            LocationReadiness.ack(ack));
            if (next == null) return;
            if (actions.loadingBoundary(next.generation, next.blocks())) shortcuts.clear();
            if (next.cancelAccepted()) exitToMainMenu = true;
        }
        runOnUiThread(
                () -> {
                    if (!destroying && hud != null && locationReadiness.isCurrent(next))
                        hud.showReadiness(next);
                });
    }

    void cancelLocationLoad(LocationReadiness.Snapshot shown) {
        MobileMailbox.Message cancel = locationReadiness.cancel(shown);
        if (cancel != null) queueMobileAction(cancel);
    }

    public void showLoading(boolean active) {
        runOnUiThread(
                () -> {
                    if (hud != null) hud.showLoading(active);
                });
    }

    public void showCinematic(boolean active) {
        runOnUiThread(
                () -> {
                    if (hud != null) hud.showCinematic(active);
                });
    }

    /** Any thread. A state message (MobileMailbox) replaces its older value; a command beyond the capacity is refused loudly. */
    void queueMobileAction(MobileMailbox.Message action) {
        if (!actions.offer(action))
            Log.e(
                    "StormInput",
                    "Engine mailbox full ("
                            + MobileMailbox.COMMAND_CAPACITY
                            + " commands): dropped "
                            + action);
    }

    /**
     * The game loses or changes its screen: the commands queued for the old one go; the state
     * messages (audio focus, graphics reload, HUD declarations) stay, the engine needs their
     * latest value (MobileMailbox).
     */
    private void dropQueuedActions() {
        actions.purgeCommands();
    }

    void requestGameplayShortcut(String action) {
        final boolean accepted;
        synchronized (loadingInputLock) {
            LocationReadiness.Snapshot loading = locationReadiness.current();
            if (!screen.isEmpty() || !resumed || (loading != null && loading.blocks())) return;
            accepted = shortcuts.offer(action, SystemClock.uptimeMillis());
        }
        releaseVirtualControls();
        Log.i("StormShortcuts", "Queued " + action + ": " + accepted);
    }

    private final CameraDrag cameraDrag = new CameraDrag();

    void moveCamera(float dx, float dy) {
        final int[] counts = cameraDrag.take(dx, dy);
        if (counts[0] != 0 || counts[1] != 0)
            onNativeMouse(0, MotionEvent.ACTION_MOVE, counts[0], counts[1], true);
    }

    void endCameraDrag() {
        cameraDrag.reset();
    }

    /** The camera pinch (CameraGesture): the engine's camera in charge applies it to its own zoom. */
    void zoomCamera(float scale) {
        if (!screen.isEmpty() || !resumed || !windowFocused) return;
        queueMobileAction(MobileMailbox.Message.cameraZoom(scale));
        traceInput("zoom " + scale);
    }

    /** The world map's camera moved by the look finger (CameraGesture): every sample, with its time. */
    void lookCamera(MobileMailbox.Message step) {
        if (!screen.isEmpty() || !resumed || !windowFocused) return;
        queueMobileAction(step);
    }

    static void traceInput(String event) {
        if (Log.isLoggable("StormInput", Log.DEBUG)) Log.d("StormInput", event);
    }

    void releaseVirtualControls() {
        if (hud != null) hud.cancelGestures();
        // The portable UI lets go of its stick and held buttons and tells the engine itself;
        // what it queued before must not act after the cancellation.
        ui.cancelInput();
        nativeCancelInput();
        traceInput("cancel all");
    }

    @Override
    protected void onPause() {
        closeUiEditor();
        resumed = false;
        publishPresentation();
        back.cancel();
        shortcuts.clear();
        releaseVirtualControls();
        dropQueuedActions();
        // The engine pauses its own audio for the lifecycle; give the output back to other apps.
        if (audioFocus != null) audioFocus.release();
        // From this thread, not SDL's pause event: SDL raises that on the game thread, which a
        // stuck game never reaches, and a regular stop would then count as a hang at the next
        // start.
        nativeLifecycle(true);
        super.onPause();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() != KeyEvent.KEYCODE_BACK || mBrokenLibraries)
            return super.dispatchKeyEvent(event);
        int source = event.getSource();
        if (source == InputDevice.SOURCE_UNKNOWN) {
            InputDevice device = event.getDevice();
            if (device != null) source = device.getSources();
        }
        // SDL reserves mouse BACK for its mouse handling, not menu navigation.
        if ((source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE)
            return super.dispatchKeyEvent(event);
        // The SDL surface consumes keyboard BACK as Escape. Own both halves
        // before it sees them, and navigate once on a matching uncancelled up.
        // Dialog windows and the IME dispatch their own BACK before this point.
        if (back.key(
                event.getAction() == KeyEvent.ACTION_DOWN,
                event.getAction() == KeyEvent.ACTION_UP,
                event.getDownTime(),
                event.getDeviceId(),
                event.getRepeatCount(),
                event.isCanceled())) onBackPressed();
        return true;
    }

    @Override
    public void onBackPressed() {
        LocationReadiness.Snapshot load = locationReadiness.current();
        switch (BackDispatch.route(
                load != null && load.blocks(),
                () -> hud != null && hud.menuTransitionPending(),
                ui.wantsBack(),
                ui.interfaceActive(),
                screen)) {
            case CANCEL_LOAD:
                cancelLocationLoad(load);
                break;
            case HELD_BY_MENU:
                break;
            case UI:
                ui.back();
                break;
            case SCREEN:
                queueMobileAction(MobileMailbox.Message.BACK);
                break;
            case MAIN_MENU:
                requestGameplayShortcut("mainmenu");
                break;
        }
    }
}
