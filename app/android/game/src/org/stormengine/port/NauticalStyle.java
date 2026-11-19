package org.stormengine.port;

/** Helpers for the few native views over the game (the touch HUD's look area). */
final class NauticalStyle {
    static void role(android.view.View view, String role) {
        view.setAccessibilityDelegate(
                new android.view.View.AccessibilityDelegate() {
                    @Override
                    public void onInitializeAccessibilityNodeInfo(
                            android.view.View host,
                            android.view.accessibility.AccessibilityNodeInfo info) {
                        super.onInitializeAccessibilityNodeInfo(host, info);
                        info.getExtras().putString("org.stormengine.port.role", role);
                    }
                });
    }

    private NauticalStyle() {}
}
