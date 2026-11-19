package org.stormengine.port;

/** The «Качество текстур» a phone starts with before the player chooses one (GraphicsSettings texture_quality), by
 *  its memory class: the game's textures at full resolution take 150-230 MB of GPU memory beside 0.9-1.4 GB for the
 *  rest of the game, too much for the phones that have 4 GB or less. Classes by
 *  ActivityManager.MemoryInfo.totalMem, which reports less than the advertised size (a 4 GB phone 3.5-3.8 GiB, a
 *  6 GB one 5.3-5.6 GiB):
 *    below 3.25 GiB (2-3 GB phones): 0 Низкое, a quarter of the sides;
 *    below 4.75 GiB (4 GB phones):   1 Среднее, half the sides;
 *    otherwise:                      2 Высокое, the game's own (also when the size is unknown, logged by the caller).
 *  «Ультра» (the neural ×2) is never a default. No Android types: host-tested (tests/android_texture_memory_class). */
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
