package org.stormengine.port;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.List;

final class GameCacheCheck {
    private static void write(File file, int bytes) throws IOException {
        file.getParentFile().mkdirs();
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(new byte[bytes]);
        }
    }

    public static void main(String[] args) throws Exception {
        if (GameCache.startShortfall(GameCache.START_FREE_BYTES, 10L << 30) != 0)
            throw new AssertionError("room on every volume: the game starts");
        if (GameCache.startShortfall(10L << 30, GameCache.START_FREE_BYTES - 1000) != 1000)
            throw new AssertionError("the fullest volume decides what to free");
        if (GameCache.startShortfall(0, 0) != GameCache.START_FREE_BYTES)
            throw new AssertionError("a full storage frees the whole start room");

        File work = new File(args[0]);
        File files = new File(work, "files"),
                cache = new File(work, "cache"),
                external = new File(work, "external-cache");

        // The root: the external cache when mounted, the internal one otherwise; never without the
        // internal one.
        assert GameCache.root(external, cache).equals(new File(external, "engine"));
        assert GameCache.root(null, cache).equals(new File(cache, "engine"));
        boolean refused = false;
        try {
            GameCache.root(external, null);
        } catch (IllegalStateException e) {
            refused = true;
        }
        assert refused;

        // The other volume's engine root is stale; the current root, saves, logs and options are
        // never listed.
        File root = GameCache.root(external, cache);
        write(new File(cache, "engine/TextureCache/a.ktx"), 512);
        write(new File(files, "storm/SaveData/slot1"), 100);
        write(new File(files, "storm/Logs/system.log"), 100);
        write(new File(files, "storm/options"), 10);
        write(new File(cache, "storm-diagnostics/report.txt"), 10);
        write(new File(root, "TextureCache/b.ktx"), 10);
        write(new File(root, "relief/live.relief"), 10);
        // A link inside the stale root: removed, its target kept.
        File outside = new File(work, "outside/keep.txt");
        write(outside, 10);
        Process link =
                new ProcessBuilder(
                                "ln",
                                "-s",
                                outside.getParentFile().getAbsolutePath(),
                                new File(cache, "engine/link").getAbsolutePath())
                        .start();
        assert link.waitFor() == 0;

        List<File> stale = GameCache.stale(cache, external, root);
        assert !stale.contains(root);
        List<GameCache.Removed> removed = GameCache.remove(stale);
        assert removed.size() == 1 && removed.get(0).bytes == 512 : removed;
        assert !new File(cache, "engine").exists();
        assert new File(files, "storm/SaveData/slot1").isFile()
                && new File(files, "storm/Logs/system.log").isFile();
        assert new File(files, "storm/options").isFile()
                && new File(cache, "storm-diagnostics/report.txt").isFile();
        assert new File(root, "TextureCache/b.ktx").isFile() && outside.isFile();
        assert new File(root, "relief/live.relief").isFile();

        // Idempotent: a second start finds nothing to remove.
        assert GameCache.remove(GameCache.stale(cache, external, root)).isEmpty();

        // Internal root (no external volume): the external volume's old root is stale instead.
        write(new File(external, "engine/ProgramCache/x.bin"), 8);
        File internal = GameCache.root(null, cache);
        write(new File(internal, "relief/live.relief"), 10);
        List<GameCache.Removed> other =
                GameCache.remove(GameCache.stale(cache, external, internal));
        assert other.size() == 1 && other.get(0).path.equals(new File(external, "engine")) : other;
        assert new File(internal, "relief/live.relief").isFile();
        System.out.println("PASS: engine cache root and removal of the other volume's cache");
    }
}
