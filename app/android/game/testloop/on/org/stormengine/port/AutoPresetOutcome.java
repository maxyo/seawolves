package org.stormengine.port;

/** The engine's final lines of the automatic graphics preset (renderer.cpp, auto_preset.hpp): a choice or why there is
 *  none. A rung's result ("graphics auto preset: preset 0: 132 frames ...") and "measuring ..." are progress, not an
 *  outcome: taking one for the outcome would let the test loop go on while the engine still measures, and leaving
 *  the title or applying a preset then aborts the measurement. */
final class AutoPresetOutcome {
    static final String CHOSE = "graphics auto preset: chose ";
    static final String[] NEEDLES = {
        CHOSE,
        "graphics auto preset: measurement aborted: ",
        "graphics auto preset: not measured this time: ",
        "graphics auto preset: off", // ", the player chose preset N" / " while STORM_FX_OPTIONS
        // ..."
        " was measured (version ", // "graphics auto preset: preset N was measured (version V,
        // current C)"
        "graphics auto preset: the app reports no fx_preset_auto",
    };

    static boolean of(String line) {
        for (String needle : NEEDLES) if (line.contains(needle)) return true;
        return false;
    }

    private AutoPresetOutcome() {}
}
