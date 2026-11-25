package org.stormengine.port;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.util.ArrayList;
import java.util.List;

/**
 * The one directory for everything the game derives and can make again: {@code (getExternalCacheDir() ?:
 * getCacheDir())/engine}. Android's «Очистить кэш» deletes it and the engine rebuilds what is missing; saves,
 * settings, the imported game and the logs stay in persistent storage.
 */
final class GameCache {
    static final String ENGINE = "engine";

    private GameCache() {}

    /**
     * External app cache when it is mounted; the internal one otherwise (the caller logs it). The system clears both
     * the same way.
     */
    static File root(File externalCache, File internalCache) {
        if (internalCache == null)
            throw new IllegalStateException("Context.getCacheDir() is unavailable");
        return new File(externalCache != null ? externalCache : internalCache, ENGINE);
    }

    /**
     * The engine root of the cache volume that is not in use (the external volume was mounted or removed since the
     * last start): removed so that derived data never takes space twice. Empty when {@code root} is the only one.
     */
    static List<File> stale(File internalCache, File externalCache, File root) {
        List<File> result = new ArrayList<>();
        for (File volume : new File[] {internalCache, externalCache}) {
            if (volume == null) continue;
            File other = new File(volume, ENGINE);
            if (!other.getAbsoluteFile().equals(root.getAbsoluteFile())) result.add(other);
        }
        return result;
    }

    /** «Очистить кэш» of the settings screen: a marker in the persistent files; the cache root is emptied at the next
     *  engine start (GameActivity.getCacheRoot), before the engine opens anything in it. */
    static final String CLEAR_MARKER = "clear-cache-on-start";

    static File clearMarker(File filesDir) {
        return new File(filesDir, CLEAR_MARKER);
    }

    /** Room a game start needs on the volume of the cache root and on that of the persistent files (logs, the session
     *  marker, settings, a save): below it the engine is not started (GameActivity) and the launcher says how much to free. */
    static final long START_FREE_BYTES = 64L << 20;

    /** Bytes still to free before the game starts, given the usable bytes of each volume it writes to; 0 when all have room. */
    static long startShortfall(long... usable) {
        long least = Long.MAX_VALUE;
        for (long bytes : usable) least = Math.min(least, bytes);
        return Math.max(0, START_FREE_BYTES - least);
    }

    /** One removed location: what it was and how many bytes it freed. */
    static final class Removed {
        final File path;
        final long bytes;

        Removed(File path, long bytes) {
            this.path = path;
            this.bytes = bytes;
        }

        @Override
        public String toString() {
            return path + " (" + (bytes >> 10) + " KiB)";
        }
    }

    /** Deletes those of {@code locations} that exist. Symbolic links are removed, never followed. */
    static List<Removed> remove(List<File> locations) throws IOException {
        List<Removed> removed = new ArrayList<>();
        for (File location : locations)
            if (Files.exists(location.toPath(), LinkOption.NOFOLLOW_LINKS))
                removed.add(new Removed(location, Trees.delete(location)));
        return removed;
    }
}
