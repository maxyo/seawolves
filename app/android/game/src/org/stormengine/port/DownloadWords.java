package org.stormengine.port;

import android.content.Context;

import java.util.Locale;

/** Download library strings from the shared menu catalogs in the app's language (MenuText.appLanguage); amounts in the UI pack's words, as the library screen says them. */
final class DownloadWords {
    static String text(Context context, String key) {
        return new MenuText(context, MenuText.appLanguage(context))
                .get("download_" + key.replace('-', '_'));
    }

    /** The notification's line for an active set: its state, then what the library row says of it. */
    static String status(Context context, GameDownloads.Entry entry) {
        MenuText words = new MenuText(context, MenuText.appLanguage(context));
        StringBuilder line =
                new StringBuilder(entry.name)
                        .append(" · ")
                        .append(words.get("download_" + entry.state.replace('-', '_')));
        if ("checking".equals(entry.state))
            line.append(" · ").append(entry.checkedPermille / 10).append('%');
        else if (entry.offline
                && ("metadata".equals(entry.state) || "downloading".equals(entry.state)))
            line.append(" · ").append(words.uiPack(context, "downloads.offline"));
        else if ("downloading".equals(entry.state) && entry.wanted > 0) {
            line.append(" · ").append(sizeOf(context, words, entry.downloaded, entry.wanted));
            if (entry.rate >= 1024) line.append(" · ").append(speed(context, words, entry.rate));
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

    private static String decimal(Context context, MenuText words, double value) {
        return String.format(Locale.ROOT, "%.1f", value)
                .replace(".", words.uiPack(context, "launcher.decimal_mark"));
    }

    private static String size(Context context, MenuText words, double bytes) {
        return bytes >= GIB / 10
                ? fill(words.uiPack(context, "launcher.gb"), decimal(context, words, bytes / GIB))
                : fill(words.uiPack(context, "launcher.mb"), decimal(context, words, bytes / MIB));
    }

    private static String sizeOf(Context context, MenuText words, double done, double total) {
        return fill(
                words.uiPack(context, "downloads.size_of"),
                decimal(context, words, done / (total >= GIB / 10 ? GIB : MIB)),
                size(context, words, total));
    }

    private static String speed(Context context, MenuText words, double rate) {
        String amount =
                rate < MIB
                        ? fill(
                                words.uiPack(context, "launcher.kb"),
                                String.valueOf((long) (rate / 1024)))
                        : size(context, words, rate);
        return fill(words.uiPack(context, "downloads.speed"), amount);
    }

    private static String fill(String text, String... arguments) {
        for (int i = 0; i < arguments.length; i++)
            text = text.replace("{" + (i + 1) + "}", arguments[i]);
        return text;
    }
}
