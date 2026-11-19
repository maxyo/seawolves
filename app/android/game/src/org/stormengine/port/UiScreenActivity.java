package org.stormengine.port;

import android.app.Activity;
import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;

/**
 * Host of one of the application's own screens (launcher, download library, about) drawn
 * by the portable UI (engine src/libs/ui/src/screens/launcher.cpp). The activity publishes
 * the raw state of its screen on the "launcher" channel; the engine formats every text and
 * sends back what the player did as {verb, value}. Back goes to the UI first: its ← (or
 * open sheet) handles it, otherwise {@link #onBack}.
 */
abstract class UiScreenActivity extends Activity implements StormUiView.Listener {
    private static final String TAG = "StormUi";
    protected StormUiView ui;
    private UiTextEditor editor;
    private final Handler main = new Handler(Looper.getMainLooper());
    private boolean publishQueued;

    /** "launcher", "downloads" or "about". */
    protected abstract String screen();

    /** The raw data of the screen (contract: engine launcher.cpp). */
    protected abstract JSONObject screenState() throws JSONException;

    /** A player action of the screen. */
    protected abstract void onAction(String verb, String value);

    /** The text the player accepted in the field `field` ("PASSWORD", "MAGNET"). */
    protected void onEdited(String field, String text) {
        Log.e(TAG, screen() + ": unexpected text for " + field);
    }

    /** Back that the screen itself did not take. */
    protected void onBack() {
        finish();
    }

    /** True when this start only brings an existing task back: the activity finishes without a UI. */
    protected boolean bringsTaskBack() {
        return false;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (bringsTaskBack()) {
            finish();
            return;
        }
        getWindow()
                .getDecorView()
                .setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(0x22, 0x20, 0x1c));
        ui = new StormUiView(this, null, "", this);
        root.addView(ui, new FrameLayout.LayoutParams(-1, -1));
        editor = new UiTextEditor(root, ui);
        setContentView(root);
        new BackNavigation(this, this::back).setEnabled(true);
    }

    @Override
    protected void onStart() {
        super.onStart();
        publish();
    }

    /**
     * One UI host per process: a finishing screen gives its host up here, not in onDestroy.
     * The release is queued before the GL thread pauses, so it runs with the context still
     * current, and Android starts the next activity only after this onPause returned: a screen
     * of this process reopened at once finds the slot free (onDestroy comes after the next
     * screen's surface). Two live screens of one process stay a loud error of the runtime.
     */
    @Override
    protected void onPause() {
        super.onPause();
        if (isFinishing()) ui.release();
        ui.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        ui.onResume();
    }

    @Override
    protected void onDestroy() {
        if (ui != null) ui.release();
        super.onDestroy();
    } // null: finished in onCreate (bringsTaskBack)

    /** API < 33; newer releases deliver Back through {@link BackNavigation}. */
    @Override
    public void onBackPressed() {
        back();
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        publish(); // the language may have changed
    }

    private void back() {
        if (editor.active()) editor.close(false);
        else ui.back();
    }

    /** Publishes the screen state once for every change made in the current main-loop turn. */
    protected final void publish() {
        if (publishQueued) return;
        publishQueued = true;
        main.post(
                () -> {
                    publishQueued = false;
                    try {
                        JSONObject state = new JSONObject();
                        state.put("screen", screen());
                        state.put("language", language());
                        state.put(screen(), screenState());
                        ui.setState("launcher", state.toString().getBytes(StandardCharsets.UTF_8));
                    } catch (JSONException error) {
                        throw new IllegalStateException(
                                "Cannot describe the " + screen() + " screen", error);
                    }
                });
    }

    /** The language of the screen's texts last published (see language()). */
    private String language;

    /**
     * The language of the screen's texts, a game language name: the app's (MenuText.appLanguage: the installed
     * game's, the phone's while no game is installed). While the game's files change (gameFilesChanging) the
     * language published before stays; afterwards a newly installed game's language takes over.
     */
    protected final String language() {
        if (language == null || !gameFilesChanging()) language = MenuText.appLanguage(this);
        return language;
    }

    /** True while an import or removal replaces the game's files (the launcher). */
    protected boolean gameFilesChanging() {
        return false;
    }

    @Override
    public void onUiAction(String node, String verb, String value, String command) {
        if (node.isEmpty() && verb.equals("back")) onBack();
        else if (node.equals("APP")) onAction(verb, value);
        else if (verb.equals("edit")) onEdited(node, value);
        else Log.e(TAG, screen() + ": unexpected action " + node + " " + verb + " " + value);
    }

    @Override
    public void onHudInput(String verb, String value, String slot, String command) {
        Log.e(TAG, screen() + ": HUD input without a HUD: " + command);
    }

    @Override
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
        editor.open(token, text, numeric, secret, x, y, width, height);
    }

    /** Without the UI runtime the application has no screens at all: say so on the screen. */
    @Override
    public void onUiFailed(String reason) {
        Log.e(TAG, reason);
        TextView message = new TextView(this);
        message.setText(reason);
        message.setTextColor(Color.rgb(0xf3, 0xdf, 0xb1));
        message.setTextSize(18);
        message.setGravity(Gravity.CENTER);
        message.setBackgroundColor(Color.rgb(0x22, 0x20, 0x1c));
        addContentView(message, new FrameLayout.LayoutParams(-1, -1));
    }
}
