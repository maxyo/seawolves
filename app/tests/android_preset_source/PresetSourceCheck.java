package org.stormengine.port;

final class PresetSourceCheck {
    private static void expect(boolean condition, String what) {
        if (!condition) throw new AssertionError(what);
    }

    public static void main(String[] arguments) {
        // First start: measured once; the engine sees the version of the stored measurement.
        String source = PresetSource.AUTO;
        expect(PresetSource.autoKey(source, 0) == 0, "never measured");
        expect(PresetSource.measurementMayStore(source), "the first measurement stores");
        expect(PresetSource.autoKey(source, 1) == 1, "measured by version 1");

        // «Готово» without touching the preset (only the frame rate, or nothing): still automatic.
        expect(
                PresetSource.afterSettings(source, 1, 1).equals(PresetSource.AUTO),
                "same preset sent back");
        expect(
                PresetSource.afterSettings(source, 1, null).equals(PresetSource.AUTO),
                "no preset sent");

        // The player changes it: manual for good.
        source = PresetSource.afterSettings(source, 1, 2);
        expect(source.equals(PresetSource.MANUAL), "a changed preset is the player's");
        expect(
                PresetSource.autoKey(source, 1) == -1 && PresetSource.autoKey(source, 99) == -1,
                "never measured again");
        expect(
                !PresetSource.measurementMayStore(source),
                "a late measurement does not store over it");
        // Going back to the measured value is still the player's choice.
        expect(
                PresetSource.afterSettings(source, 2, 1).equals(PresetSource.MANUAL),
                "manual stays manual");
        expect(
                PresetSource.afterSettings(source, 2, 2).equals(PresetSource.MANUAL),
                "manual stays manual (same)");
        // The measured rung below «Низкая»: its scene height only with «Низкая» and while the
        // choice is automatic.
        expect(
                PresetSource.autoRenderHeight(PresetSource.AUTO, 0, 720) == 720,
                "automatic «Низкая» at 720 lines");
        expect(
                PresetSource.autoRenderHeight(PresetSource.AUTO, 0, 0) == 0,
                "automatic «Низкая» at its own height");
        expect(
                PresetSource.autoRenderHeight(PresetSource.AUTO, 1, 720) == 0,
                "a height only with «Низкая»");
        expect(
                PresetSource.autoRenderHeight(PresetSource.MANUAL, 0, 720) == 0,
                "the player's choice resets the height");
        expect(
                PresetSource.autoRenderHeight(
                                PresetSource.afterSettings(PresetSource.AUTO, 0, 1), 0, 720)
                        == 0,
                "the player changed the preset: no measured height, even back on «Низкая»");
        expect(
                PresetSource.autoRenderHeight(
                                PresetSource.afterSettings(PresetSource.AUTO, 0, 0), 0, 720)
                        == 720,
                "«Готово» without a preset change keeps it");
        expect(
                PresetSource.measuredName("Низкая", 720).equals("Низкая · 720"),
                "the toast names the rung below «Низкая»");
        expect(
                PresetSource.measuredName("Средняя", 0).equals("Средняя"),
                "a preset at its own height by its name");
        System.out.println(
                "PASS graphics preset source: first measurement, settings without a change, manual for good, the measured scene height and its name");
    }
}
