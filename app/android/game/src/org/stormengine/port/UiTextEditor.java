package org.stormengine.port;

import android.content.Context;
import android.graphics.Color;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

/**
 * IME bridge of the portable UI: a native editor laid exactly over the text field the UI
 * drew, shown with the system keyboard. Done submits the text; Back or a touch outside
 * dismisses it. One editor at a time.
 */
final class UiTextEditor {
    /** Where the answer goes: the UI host that asked (Host::SubmitText). */
    interface Answer {
        void submitText(String token, String text, boolean accepted);
    }

    private final ViewGroup host;
    private final Answer ui;
    private FrameLayout layer;
    private String token;

    UiTextEditor(ViewGroup host, Answer ui) {
        this.host = host;
        this.ui = ui;
    }

    void open(
            String requestToken,
            String text,
            boolean numeric,
            boolean secret,
            int x,
            int y,
            int width,
            int height) {
        close(false);
        token = requestToken;
        final Context context = host.getContext();
        final EditText input =
                new EditText(context) {
                    @Override
                    public boolean onKeyPreIme(int key, KeyEvent event) {
                        if (key == KeyEvent.KEYCODE_BACK
                                && event.getAction() == KeyEvent.ACTION_UP) {
                            close(false);
                            return true;
                        }
                        return super.onKeyPreIme(key, event);
                    }
                };
        input.setSingleLine(true);
        input.setText(text);
        input.setSelectAllOnFocus(true);
        input.setInputType(
                numeric
                        ? InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED
                        : secret
                                ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD
                                : InputType.TYPE_CLASS_TEXT);
        input.setImeOptions(
                EditorInfo.IME_ACTION_DONE
                        | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                        | EditorInfo.IME_FLAG_NO_FULLSCREEN);
        input.setTextColor(Color.rgb(0xf3, 0xdf, 0xb1));
        input.setBackgroundColor(Color.rgb(0x0f, 0x1b, 0x20));
        input.setPadding(Math.round(height * 0.25f), 0, Math.round(height * 0.25f), 0);
        input.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX, height * 0.42f);
        input.setOnEditorActionListener(
                (view, action, event) -> {
                    if (action != EditorInfo.IME_ACTION_DONE && event == null) return false;
                    final String value = input.getText().toString();
                    close(true, value);
                    return true;
                });
        layer = new FrameLayout(context);
        layer.setOnClickListener(view -> close(false));
        final FrameLayout.LayoutParams box =
                new FrameLayout.LayoutParams(Math.max(width, height * 3), height);
        box.leftMargin = x;
        box.topMargin = y;
        layer.addView(input, box);
        host.addView(layer, new ViewGroup.LayoutParams(-1, -1));
        input.requestFocus();
        input.post(
                () -> {
                    InputMethodManager keyboard =
                            (InputMethodManager)
                                    context.getSystemService(Context.INPUT_METHOD_SERVICE);
                    if (keyboard != null)
                        keyboard.showSoftInput(input, InputMethodManager.SHOW_IMPLICIT);
                });
    }

    boolean active() {
        return layer != null;
    }

    void close(boolean accepted) {
        close(accepted, "");
    }

    private void close(boolean accepted, String text) {
        if (layer == null) return;
        final View focused = layer.findFocus();
        InputMethodManager keyboard =
                (InputMethodManager)
                        host.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if (keyboard != null && focused != null)
            keyboard.hideSoftInputFromWindow(focused.getWindowToken(), 0);
        host.removeView(layer);
        layer = null;
        ui.submitText(token, text, accepted);
    }
}
