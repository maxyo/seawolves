package org.stormengine.port;

/** Host checks for TextureMemoryClass: the texture level a phone starts with by its reported memory. */
public final class TextureMemoryClassCheck {
    private static final long GIB = 1024L * 1024L * 1024L, MIB = 1024L * 1024L;

    private static void level(long bytes, int expected, String what) {
        int got = TextureMemoryClass.defaultLevel(bytes);
        if (got != expected)
            throw new AssertionError(what + ": level " + got + ", expected " + expected);
    }

    public static void main(String[] args) {
        level(0, 2, "unknown size: Высокое");
        level(-1, 2, "unknown size (negative): Высокое");
        level(1800 * MIB, 0, "2 GB phone: Низкое");
        level(2800 * MIB, 0, "3 GB phone (about 2.7 GiB reported): Низкое");
        level(3600 * MIB, 1, "4 GB phone (about 3.5 GiB reported): Среднее");
        level(3900 * MIB, 1, "4 GB phone (3.8 GiB reported): Среднее");
        level(5400 * MIB, 2, "6 GB phone (about 5.3 GiB reported): Высокое");
        level(7400 * MIB, 2, "8 GB phone: Высокое");
        level(15 * GIB, 2, "16 GB phone: Высокое");
        level(TextureMemoryClass.LOW_BELOW - 1, 0, "just below the low bound");
        level(TextureMemoryClass.LOW_BELOW, 1, "the low bound");
        level(TextureMemoryClass.MEDIUM_BELOW - 1, 1, "just below the medium bound");
        level(TextureMemoryClass.MEDIUM_BELOW, 2, "the medium bound");
        System.out.println(
                "PASS: texture level by memory class (unknown, 2/3/4/6/8/16 GB phones, bounds)");
    }
}
