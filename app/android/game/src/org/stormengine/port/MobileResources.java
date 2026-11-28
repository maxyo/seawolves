package org.stormengine.port;

import org.json.JSONObject;

import java.io.*;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.util.Iterator;

/**
 * Install only the APK's licensed layers (UI source, engine shaders, script fixes), never copy the user's game. They
 * live under the cache root (GameCache, {@code layers/<layer>/current}): the system's «Очистить кэш» may remove them,
 * also while the game runs, and the next install puts them back at the same paths (GameActivity.resumeNativeThread).
 */
final class MobileResources {
    static final String LAYERS = "layers";
    private static final String[] NAMES = {"mobile-ui", "engine-shaders", "script-patches"};

    /** The APK's assets (AssetManager.open). */
    interface Assets {
        InputStream open(String path) throws IOException;
    }

    private final File layers;
    private final Assets assets;

    /** The layers under the engine's cache root (GameCache.root). */
    MobileResources(File cacheRoot, Assets assets) {
        this.layers = new File(cacheRoot, LAYERS);
        this.assets = assets;
    }

    private static String hash(byte[] bytes) throws Exception {
        StringBuilder result = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(bytes))
            result.append(String.format("%02x", b & 255));
        return result.toString();
    }

    void install() throws Exception {
        for (String layer : NAMES) installLayer(layer);
    }

    /**
     * Every layer is in place, file by file: the system may also remove only some files of the cache when the storage
     * runs low.
     */
    boolean installed() throws Exception {
        for (String layer : NAMES) {
            byte[] manifest =
                    Streams.read(assets.open(layer + "/manifest.json"), Integer.MAX_VALUE);
            if (!complete(current(layer), manifest)) return false;
        }
        return true;
    }

    /** The installed copy is of this manifest (its version stamp) and has every file the manifest names. */
    private static boolean complete(File current, byte[] manifest) throws Exception {
        File stamp = new File(current, ".version");
        if (!stamp.isFile()
                || !new String(Files.readAllBytes(stamp.toPath()), StandardCharsets.UTF_8)
                        .equals(hash(manifest))) return false;
        JSONObject files = new JSONObject(new String(manifest, StandardCharsets.UTF_8));
        for (Iterator<String> keys = files.keys(); keys.hasNext(); )
            if (!new File(current, keys.next()).isFile()) return false;
        return true;
    }

    /**
     * The script patch layer, installed from the APK if it is not yet (or an older one is): the directory the engine
     * applies at start-up and the archive reader identifies game copies against before any discovery
     * (ArchiveInstaller), so both use the same sets. Call off the main thread.
     */
    File scriptPatches() throws Exception {
        installLayer("script-patches");
        return current("script-patches");
    }

    private File current(String layer) {
        return new File(new File(layers, layer), "current");
    }

    /**
     * Copies the layer's files (the names of its manifest.json) out of the APK when the installed copy's version (the
     * manifest's hash) differs. The APK's signature covers every asset. Synchronized and under a file lock: the
     * launcher's discovery, the import and «Играть» may install a layer at the same time, and the game's process
     * installs at its start and resume while the launcher's may.
     */
    private void installLayer(String layer) throws Exception {
        File root = new File(layers, layer);
        synchronized (MobileResources.class) {
            if (!root.isDirectory() && !root.mkdirs())
                throw new IOException("Cannot create the layer directory " + root);
            try (FileChannel channel =
                            FileChannel.open(
                                    new File(root, ".lock").toPath(),
                                    StandardOpenOption.CREATE,
                                    StandardOpenOption.WRITE);
                    FileLock lock = channel.lock()) {
                installLocked(layer, root);
            }
        }
    }

    private void installLocked(String layer, File root) throws Exception {
        byte[] manifest = Streams.read(assets.open(layer + "/manifest.json"), Integer.MAX_VALUE);
        String version = hash(manifest);
        File current = new File(root, "current"),
                staging = new File(root, "staging"),
                previous = new File(root, "previous");
        if (!current.exists() && previous.exists() && !previous.renameTo(current))
            throw new IOException("Cannot recover UI layer");
        if (complete(current, manifest)) return;
        Trees.delete(staging);
        if (!staging.mkdirs()) throw new IOException("Cannot stage UI layer");
        JSONObject files = new JSONObject(new String(manifest, StandardCharsets.UTF_8));
        for (Iterator<String> keys = files.keys(); keys.hasNext(); ) {
            String name = keys.next();
            File target = new File(staging, name);
            File parent = target.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs())
                throw new IOException("Cannot stage " + layer + " file " + name);
            try (InputStream in = assets.open(layer + "/" + name)) {
                Files.copy(in, target.toPath());
            }
        }
        Files.write(
                new File(staging, ".version").toPath(), version.getBytes(StandardCharsets.UTF_8));
        Trees.delete(previous);
        if (current.exists() && !current.renameTo(previous))
            throw new IOException("Cannot update UI layer");
        if (!staging.renameTo(current)) {
            previous.renameTo(current);
            throw new IOException("Cannot activate UI layer");
        }
        Trees.delete(previous);
    }
}
