package android.util;

import java.util.ArrayList;
import java.util.List;

/** Records log lines so host checks can require a warning; also prints them. */
public final class Log {
    public static final List<String> lines = new ArrayList<>();

    private Log() {}

    private static int add(char level, String tag, String message, Throwable error) {
        String line = level + "/" + tag + ": " + message + (error == null ? "" : " | " + error);
        synchronized (lines) {
            lines.add(line);
        }
        System.err.println(line);
        return 0;
    }

    public static int d(String tag, String message) {
        return add('D', tag, message, null);
    }

    public static int i(String tag, String message) {
        return add('I', tag, message, null);
    }

    public static int w(String tag, String message) {
        return add('W', tag, message, null);
    }

    public static int w(String tag, String message, Throwable error) {
        return add('W', tag, message, error);
    }

    public static int e(String tag, String message) {
        return add('E', tag, message, null);
    }

    public static int e(String tag, String message, Throwable error) {
        return add('E', tag, message, error);
    }
}
