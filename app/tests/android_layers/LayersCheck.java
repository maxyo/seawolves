package org.stormengine.port;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** MobileResources: layers a clear of the cache removed, or that were damaged, come back at the same paths. */
public final class LayersCheck {
    private static void expect(boolean ok, String what) {
        if (!ok) throw new AssertionError(what);
    }

    private static void write(File file, String text) throws IOException {
        file.getParentFile().mkdirs();
        Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    public static void main(String[] args) throws Exception {
        File work = new File(args[0]),
                apk = new File(work, "assets"),
                cache = new File(work, "cache");
        for (String layer : new String[] {"mobile-ui", "engine-shaders", "script-patches"}) {
            write(new File(apk, layer + "/manifest.json"), "{\"PROGRAM/" + layer + ".c\":\"x\"}");
            write(new File(apk, layer + "/PROGRAM/" + layer + ".c"), "// " + layer);
        }
        MobileResources layers =
                new MobileResources(cache, path -> new FileInputStream(new File(apk, path)));
        expect(layers.differs() != null, "nothing installed yet");
        layers.install();
        expect(layers.differs() == null, "installed");
        File ui = new File(cache, "layers/mobile-ui/current/PROGRAM/mobile-ui.c");
        expect(ui.isFile(), "the engine's path of a UI file");

        // The system clears the cache while the game is in the background.
        Trees.delete(cache);
        expect(layers.differs() != null, "a cleared cache is seen");
        layers.install();
        expect(layers.differs() == null && ui.isFile(), "back at the same path");

        // One layer gone is enough to install again.
        Trees.delete(new File(cache, "layers/script-patches"));
        expect(layers.differs() != null, "a missing layer is seen");
        layers.install();
        expect(
                new File(cache, "layers/script-patches/current/PROGRAM/script-patches.c").isFile(),
                "restored");
        // The system removed some files of the cache when the storage ran low: the stamp is still
        // there.
        expect(ui.delete(), "a UI file removed");
        String missing = layers.differs();
        expect(
                missing != null && missing.contains("PROGRAM/mobile-ui.c is missing"),
                "the missing file is named: " + missing);
        layers.install();
        expect(layers.differs() == null && ui.isFile(), "the file is back");

        // A damaged layer (the engine refuses it at start): a stray entry, a stray directory. Each
        // is named and the
        // layer is the APK's again.
        File patches = new File(cache, "layers/script-patches/current");
        File stray = new File(patches, "sets/stray.txt");
        write(stray, "not the layer's");
        String damaged = layers.differs();
        expect(
                damaged != null && damaged.contains("sets is not the layer's"),
                "a stray entry is named: " + damaged);
        layers.install();
        expect(layers.differs() == null && !stray.exists(), "the stray entry is gone");
        File strayDirectory = new File(patches, "PROGRAM/extra");
        expect(strayDirectory.mkdirs(), "a stray directory");
        damaged = layers.differs();
        expect(
                damaged != null && damaged.contains("PROGRAM/extra is not the layer's"),
                "a stray directory is named: " + damaged);
        layers.install();
        expect(layers.differs() == null && !strayDirectory.exists(), "the stray directory is gone");
        System.out.println(
                "PASS layers: a cleared, trimmed or damaged layer is seen and comes back at the same paths");
    }
}
