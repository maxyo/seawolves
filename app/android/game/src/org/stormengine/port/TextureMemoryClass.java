package org.stormengine.port;

/**
 * The «Качество текстур» a phone starts with before the player chooses, by its memory class: full-resolution
 * textures take 150-230 MB of GPU memory beside 0.9-1.4 GB for the rest of the game, too much for 4 GB phones.
 * ActivityManager reports less than the advertised size (a 4 GB phone 3.5-3.8 GiB), hence the thresholds.
 */
final class TextureMemoryClass {
    private static final long GIB = 1024L * 1024L * 1024L;
    static final long LOW_BELOW = 13L * GIB / 4, MEDIUM_BELOW = 19L * GIB / 4;

    private TextureMemoryClass() {}

    /** The default level for `totalBytes` of memory (0 or less: unknown). */
    static int defaultLevel(long totalBytes) {
        if (totalBytes <= 0) return 2;
        if (totalBytes < LOW_BELOW) return 0;
        if (totalBytes < MEDIUM_BELOW) return 1;
        return 2;
    }
}
