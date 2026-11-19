package org.stormengine.port;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * Cross-process activity flags. The download library runs in its own process,
 * so the game and the installer cannot share static state. A flag records the
 * owner's PID and process name and becomes stale when that process dies.
 */
final class ProcessMarker {
    static final String ENGINE = "engine", IMPORT = "import";

    /** True while a game import in another process (the launcher's ImportCoordinator) is replacing the game. */
    static boolean importing() {
        return activeElsewhere(IMPORT);
    }

    private static volatile File root;

    private ProcessMarker() {}

    /** Called once per process by GameApplication. Host tests leave it unset. */
    static void init(File directory) {
        if (directory != null && (directory.isDirectory() || directory.mkdirs())) root = directory;
    }

    static synchronized void set(String name, boolean active) {
        File directory = root;
        if (directory == null) return;
        File marker = new File(directory, name + ".pid");
        String self = selfPid();
        try {
            if (active) {
                File temp = new File(directory, name + ".tmp");
                try (FileOutputStream out = new FileOutputStream(temp)) {
                    out.write((self + "\n" + processName(self)).getBytes(StandardCharsets.UTF_8));
                }
                if (!temp.renameTo(marker)) throw new IOException("Cannot publish process marker");
            } else {
                String[] owner = read(marker);
                if (owner != null && owner[0].equals(self)) marker.delete();
            }
        } catch (IOException error) {
            // Host-testable: no android.util dependency. System.err reaches logcat.
            System.err.println("StormProcess: cannot update marker " + name + ": " + error);
        }
    }

    /** True only when another live process of this app holds the flag. */
    static boolean activeElsewhere(String name) {
        File directory = root;
        if (directory == null) return false;
        String[] owner = read(new File(directory, name + ".pid"));
        if (owner == null || owner[0].equals(selfPid())) return false;
        return !owner[1].isEmpty() && owner[1].equals(processName(owner[0]));
    }

    private static String[] read(File marker) {
        try (FileInputStream in = new FileInputStream(marker)) {
            byte[] data = new byte[512];
            int n = in.read(data);
            if (n <= 0) return null;
            String[] parts = new String(data, 0, n, StandardCharsets.UTF_8).split("\n", 2);
            return parts.length == 2 && parts[0].matches("[0-9]+") ? parts : null;
        } catch (IOException missing) {
            return null;
        }
    }

    private static String selfPid() {
        try {
            return new File("/proc/self").getCanonicalFile().getName();
        } catch (IOException unavailable) {
            return "";
        }
    }

    private static String processName(String pid) {
        try (FileInputStream in = new FileInputStream("/proc/" + pid + "/cmdline")) {
            byte[] data = new byte[256];
            int n = in.read(data), end = 0;
            while (end < Math.max(0, n) && data[end] != 0) end++;
            return new String(data, 0, end, StandardCharsets.UTF_8);
        } catch (IOException gone) {
            return "";
        }
    }
}
