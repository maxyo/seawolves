package org.stormengine.port;

import android.content.Context;

import org.json.JSONObject;

import java.io.*;
import java.security.MessageDigest;
import java.util.Iterator;

/** Install only the APK's licensed layers (UI source, engine shaders, script fixes), never copy the user's game. */
final class MobileResources {
    private static byte[] read(InputStream stream) throws Exception {
        try (InputStream input = stream;
                ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (out.size() + count > 2 * 1024 * 1024)
                    throw new IOException("UI source exceeds size limit");
                out.write(buffer, 0, count);
            }
            return out.toByteArray();
        }
    }

    private static String hash(byte[] bytes) throws Exception {
        StringBuilder result = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(bytes))
            result.append(String.format("%02x", b & 255));
        return result.toString();
    }

    private static void remove(File file) throws IOException {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) remove(child);
        if (file.exists() && !file.delete())
            throw new IOException("Cannot remove old UI source: " + file);
    }

    static void install(Context context) throws Exception {
        installLayer(context, "mobile-ui");
        installLayer(context, "engine-shaders");
        scriptPatches(context);
    }

    /**
     * The script patch layer, installed from the APK if it is not yet (or an older one is): the directory the engine
     * applies at start-up and the archive reader identifies game copies against before any discovery
     * (ArchiveInstaller), so both use the same sets. Call off the main thread.
     */
    static File scriptPatches(Context context) throws Exception {
        installLayer(context, "script-patches");
        return new File(context.getFilesDir(), "script-patches/current");
    }

    /** Synchronized: the launcher's discovery, the import and «Играть» may install a layer at the same time. */
    private static synchronized void installLayer(Context context, String layer) throws Exception {
        byte[] manifest = read(context.getAssets().open(layer + "/manifest.json"));
        String version = hash(manifest);
        File root = new File(context.getFilesDir(), layer);
        if (!root.isDirectory() && !root.mkdirs())
            throw new IOException("Cannot create UI directory");
        File current = new File(root, "current"),
                staging = new File(root, "staging"),
                previous = new File(root, "previous");
        if (!current.exists() && previous.exists() && !previous.renameTo(current))
            throw new IOException("Cannot recover UI layer");
        File stamp = new File(current, ".version");
        if (stamp.isFile() && new String(read(new FileInputStream(stamp)), "UTF-8").equals(version))
            return;
        remove(staging);
        if (!staging.mkdirs()) throw new IOException("Cannot stage UI layer");
        JSONObject hashes = new JSONObject(new String(manifest, "UTF-8"));
        int total = 0;
        for (Iterator<String> keys = hashes.keys(); keys.hasNext(); ) {
            String name = keys.next();
            File target = new File(staging, name).getCanonicalFile();
            if (!target.getPath().startsWith(staging.getCanonicalPath() + File.separator))
                throw new IOException("Invalid UI source path");
            byte[] data = read(context.getAssets().open(layer + "/" + name));
            total += data.length;
            if (total > 16 * 1024 * 1024 || !hash(data).equals(hashes.getString(name)))
                throw new IOException("UI source verification failed");
            File parent = target.getParentFile();
            if (!parent.isDirectory() && !parent.mkdirs())
                throw new IOException("Cannot stage UI source");
            try (FileOutputStream out = new FileOutputStream(target)) {
                out.write(data);
            }
        }
        try (FileOutputStream out = new FileOutputStream(new File(staging, ".version"))) {
            out.write(version.getBytes("UTF-8"));
        }
        remove(previous);
        if (current.exists() && !current.renameTo(previous))
            throw new IOException("Cannot update UI layer");
        if (!staging.renameTo(current)) {
            previous.renameTo(current);
            throw new IOException("Cannot activate UI layer");
        }
        remove(previous);
    }
}
