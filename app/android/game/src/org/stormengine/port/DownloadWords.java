package org.stormengine.port;

import android.content.Context;

import java.util.Locale;

/** Download library notification texts in the app's language (MenuText.appLanguage), as the library screen says them. */
final class DownloadWords {
    static String text(Context context, String key) {
        return new MenuText(context, MenuText.appLanguage(context))
                .get("host.download_" + key.replace('-', '_'));
    }

    /** The notification's line for an active set: its state, then what the library row says of it. */
    static String status(Context context, GameDownloads.Entry entry) {
        MenuText words = new MenuText(context, MenuText.appLanguage(context));
        StringBuilder line =
                new StringBuilder(entry.name)
                        .append(" · ")
                        .append(words.get("host.download_" + entry.state.replace('-', '_')));
        if ("checking".equals(entry.state))
            line.append(" · ").append(entry.checkedPermille / 10).append('%');
        else if (entry.offline
                && ("metadata".equals(entry.state) || "downloading".equals(entry.state)))
            line.append(" · ").append(words.get("downloads.offline"));
        else if ("downloading".equals(entry.state) && entry.wanted > 0) {
            line.append(" · ").append(sizeOf(words, entry.downloaded, entry.wanted));
            if (entry.rate >= 1024) line.append(" · ").append(speed(words, entry.rate));
        }
        return line.toString();
    }

    /** Thousandths of the bar the notification shows, or -1 for none. */
    static int permille(GameDownloads.Entry entry) {
        if ("checking".equals(entry.state)) return (int) entry.checkedPermille;
        if ("downloading".equals(entry.state) && entry.wanted > 0)
            return (int) Math.min(1000, entry.downloaded * 1000 / entry.wanted);
        return -1;
    }

    private static final double MIB = 1048576.0, GIB = 1073741824.0;

    private static String decimal(MenuText words, double value) {
        return String.format(Locale.ROOT, "%.1f", value)
                .replace(".", words.get("launcher.decimal_mark"));
    }

    private static String size(MenuText words, double bytes) {
        return bytes >= GIB / 10
                ? words.format("launcher.gb", decimal(words, bytes / GIB))
                : words.format("launcher.mb", decimal(words, bytes / MIB));
    }

    private static String sizeOf(MenuText words, double done, double total) {
        return words.format(
                "downloads.size_of",
                decimal(words, done / (total >= GIB / 10 ? GIB : MIB)),
                size(words, total));
    }

    private static String speed(MenuText words, double rate) {
        String amount =
                rate < MIB
                        ? words.format("launcher.kb", String.valueOf((long) (rate / 1024)))
                        : size(words, rate);
        return words.format("downloads.speed", amount);
    }
}
