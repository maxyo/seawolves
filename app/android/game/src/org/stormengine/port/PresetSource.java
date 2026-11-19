package org.stormengine.port;

/** Who chose the graphics preset (GraphicsSettings key fx_preset): the engine's automatic measurement
 *  (engine renderer auto_preset.hpp) or the player. The player's choice is final: no measurement stores over it.
 *  The measurement may also choose «Низкая» with the scene at a lower height (its rung below «Низкая»), which only
 *  holds while its automatic choice does.
 *  Host-testable, no Android types (tests/android_preset_source). */
final class PresetSource {
    static final String AUTO = "auto", MANUAL = "manual";

    private PresetSource() {}

    /** The source after the settings screen stored `next` over `stored`: the player's once the preset changed. */
    static String afterSettings(String source, int stored, Integer next) {
        return next != null && next != stored ? MANUAL : source;
    }

    /** What the engine asks for as fx_preset_auto: -1 for the player's choice, else the version of the measurement
     *  that chose the stored preset (0: never measured). */
    static int autoKey(String source, int measuredVersion) {
        return MANUAL.equals(source) ? -1 : measuredVersion;
    }

    /** A measurement's choice is stored only over an automatic (or not yet measured) preset. */
    static boolean measurementMayStore(String source) {
        return !MANUAL.equals(source);
    }

    /** The scene height the engine applies for the measured rung below «Низкая» (fx_auto_render_height): the stored
     *  height while the automatic choice stands and it is «Низкая»; 0 (the preset's own) otherwise. */
    static int autoRenderHeight(String source, int preset, int storedHeight) {
        return AUTO.equals(source) && preset == 0 && storedHeight > 0 ? storedHeight : 0;
    }

    /** The measured choice as the toast names it: the preset, with the scene height of the rung below «Низкая». */
    static String measuredName(String presetName, int renderHeight) {
        return renderHeight > 0 ? presetName + " · " + renderHeight : presetName;
    }
}
