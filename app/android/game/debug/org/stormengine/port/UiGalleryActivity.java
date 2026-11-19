package org.stormengine.port;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Debug gallery of the shared UI components: one page per component, every state.
 *
 *   adb shell am start -n com.goodoldgames.seawolves/org.stormengine.port.UiGalleryActivity \
 *       [--es doc gallery/button.rml] [--ez bare true]
 *
 * `bare` hides the page switcher so that screenshots hold only the UI itself.
 * Component actions are echoed as a toast of the UI.
 */
public final class UiGalleryActivity extends Activity implements StormUiView.Listener {
    private final List<String> pages = new ArrayList<>();
    private StormUiView ui;
    private TextView caption;
    private int page;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow()
                .getDecorView()
                .setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        readPages();
        String wanted = getIntent().getStringExtra("doc");
        if (state != null) wanted = state.getString("doc", wanted);
        page = Math.max(0, pages.indexOf(wanted));
        if (wanted != null && !pages.contains(wanted)) {
            pages.add(wanted);
            page = pages.size() - 1;
        }

        String gameRoot = "";
        try {
            gameRoot = GameStorage.readyPath(this);
        } catch (Exception noGameYet) {
            /* placeholders stay */
        }
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(0x22, 0x20, 0x1c));
        ui = new StormUiView(this, pages.isEmpty() ? "" : pages.get(page), gameRoot, this);
        root.addView(ui, new FrameLayout.LayoutParams(-1, -1));

        if (!getIntent().getBooleanExtra("bare", false)) {
            LinearLayout bar = new LinearLayout(this);
            bar.setGravity(Gravity.CENTER_VERTICAL);
            bar.setBackgroundColor(Color.argb(170, 0, 0, 0));
            bar.addView(step("‹", -1));
            caption = new TextView(this);
            caption.setTextColor(Color.rgb(0xf3, 0xdf, 0xb1));
            caption.setTextSize(13);
            caption.setPadding(dp(8), 0, dp(8), 0);
            bar.addView(caption);
            bar.addView(step("›", 1));
            root.addView(
                    bar, new FrameLayout.LayoutParams(-2, dp(48), Gravity.BOTTOM | Gravity.END));
        }
        setContentView(root);
        showCaption();
    }

    private TextView step(String label, final int direction) {
        TextView button = new TextView(this);
        button.setText(label);
        button.setTextSize(24);
        button.setGravity(Gravity.CENTER);
        button.setTextColor(Color.rgb(0xe6, 0xc4, 0x7a));
        button.setMinWidth(dp(48));
        button.setMinHeight(dp(48));
        button.setOnClickListener(
                new View.OnClickListener() {
                    @Override
                    public void onClick(View view) {
                        if (pages.isEmpty()) return;
                        page = (page + direction + pages.size()) % pages.size();
                        ui.loadDocument(pages.get(page));
                        showCaption();
                    }
                });
        return button;
    }

    private void showCaption() {
        if (caption == null) return;
        if (pages.isEmpty()) {
            caption.setText("no gallery pages in assets/ui/pack.json");
            return;
        }
        String name = pages.get(page);
        caption.setText(
                name.substring(name.lastIndexOf('/') + 1).replace(".rml", "")
                        + "  "
                        + (page + 1)
                        + "/"
                        + pages.size());
    }

    private void readPages() {
        try (InputStream in = getAssets().open("ui/pack.json")) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            for (int count; (count = in.read(buffer)) > 0; ) bytes.write(buffer, 0, count);
            JSONArray list = new JSONObject(bytes.toString("UTF-8")).optJSONArray("gallery");
            for (int i = 0; list != null && i < list.length(); i++) pages.add(list.getString(i));
        } catch (Exception missing) {
            pages.clear();
        }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    public void onUiAction(String id, String verb, String value, String command) {
        ui.toast(
                (id.isEmpty() ? "?" : id) + " · " + verb + (value.isEmpty() ? "" : " " + value),
                "info");
    }

    /** The gallery shows documents, never the game's HUD mount: there is no HUD input to receive. */
    @Override
    public void onHudInput(String verb, String value, String slot, String command) {}

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
        ui.submitText(token, text, false);
    }

    @Override
    public void onUiFailed(String reason) {
        if (caption != null) caption.setText(reason);
        android.util.Log.e("StormUi", reason);
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        if (!pages.isEmpty()) state.putString("doc", pages.get(page));
    }

    @Override
    protected void onPause() {
        super.onPause();
        ui.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        ui.onResume();
    }

    @Override
    protected void onDestroy() {
        ui.release();
        super.onDestroy();
    }
}
