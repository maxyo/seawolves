package org.stormengine.port;

import android.content.Context;
import android.content.res.Configuration;
import android.os.Build;
import android.util.Log;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** Texts the Java host shows itself: in game in the running game's language, elsewhere in the app's language
 *  ({@link #appLanguage}: the installed game's, the phone's while none is installed). */
final class MenuText {
    private static final HashMap<String, JSONObject> catalogs = new HashMap<>();
    private final JSONObject english, translated;
    final String language;
    private final Locale locale;

    MenuText(Context context, String gameLanguage) {
        locale =
                gameLanguage == null || gameLanguage.trim().isEmpty()
                        ? contextLocale(context)
                        : Locale.forLanguageTag(GameLanguage.code(gameLanguage));
        language = locale.getLanguage();
        english = load(context, "locales", "en");
        translated = load(context, "locales", language);
    }

    private static Locale contextLocale(Context context) {
        Configuration configuration = context.getResources().getConfiguration();
        if (Build.VERSION.SDK_INT >= 24 && !configuration.getLocales().isEmpty())
            return configuration.getLocales().get(0);
        return configuration.locale == null ? Locale.getDefault() : configuration.locale;
    }

    private static Set<String> uiPackCodes;
    private static String loggedProblem;

    /**
     * The language of the app's screens and notifications now (GameLanguage.choose), a game language name: the
     * installed game's, the phone's while no game is installed. An installed game whose language is missing or
     * that the UI pack has no strings for is logged as an error (once per problem) and leaves the phone's.
     */
    static synchronized String appLanguage(Context context) {
        String path = null;
        try {
            path = GameStorage.readyPath(context);
        } catch (java.io.IOException noGame) {
            /* no ready game copy (setup, import, recovery): the phone's language */
        }
        String game = null;
        StringBuilder problem = new StringBuilder();
        if (path != null) {
            try {
                game = GameLanguage.of(new java.io.File(path));
            } catch (java.io.IOException error) {
                problem.append("cannot read the installed game's language.ini (")
                        .append(error)
                        .append("); ");
            }
        }
        String language =
                GameLanguage.choose(
                        path != null,
                        game,
                        uiPackCodes(context),
                        GameLanguage.name(contextLocale(context).getLanguage()),
                        problem);
        String said = problem.toString();
        if (!said.isEmpty() && !said.equals(loggedProblem)) Log.e("StormLanguage", said);
        loggedProblem = said.isEmpty() ? null : said;
        return language;
    }

    /** The ISO codes the UI pack has strings for (assets/ui/strings/<code>.json). */
    private static Set<String> uiPackCodes(Context context) {
        if (uiPackCodes != null) return uiPackCodes;
        String[] files;
        try {
            files = context.getAssets().list("ui/strings");
        } catch (java.io.IOException error) {
            throw new IllegalStateException("Cannot list the UI pack's strings", error);
        }
        Set<String> codes = new HashSet<>();
        if (files != null)
            for (String file : files)
                if (file.endsWith(".json")) codes.add(file.substring(0, file.length() - 5));
        if (!codes.contains("en"))
            throw new IllegalStateException("The UI pack has no English strings");
        return uiPackCodes = codes;
    }

    /** `directory`/`language`.json of the assets (an app catalog, or the UI pack's strings); English must exist. */
    private static synchronized JSONObject load(
            Context context, String directory, String language) {
        JSONObject cached = catalogs.get(directory + "/" + language);
        if (cached != null) return cached;
        JSONObject result = new JSONObject();
        if (language.matches("[a-z]{2,3}")) {
            try (InputStream input =
                    context.getAssets().open(directory + "/" + language + ".json")) {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                byte[] buffer = new byte[4096];
                int n;
                while ((n = input.read(buffer)) != -1) {
                    if (bytes.size() + n > 65536)
                        throw new java.io.IOException("Catalog too large");
                    bytes.write(buffer, 0, n);
                }
                result = new JSONObject(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
            } catch (Exception error) {
                if (language.equals("en"))
                    throw new IllegalStateException("Missing English catalog " + directory, error);
            }
        }
        catalogs.put(directory + "/" + language, result);
        return result;
    }

    /** A string of the portable UI's pack (assets/ui/strings, mobile-ui/design/strings) in this text's language,
     *  English when that language has none; an absent key is an error (the pack's build checks the keys it ships). */
    String uiPack(Context context, String key) {
        for (String code : new String[] {language, "en"}) {
            JSONObject table = load(context, "ui/strings", code);
            if (table.has(key)) return table.optString(key);
        }
        throw new IllegalStateException("UI pack string " + key + " is missing");
    }

    String get(String key) {
        return translated.optString(key, english.optString(key, key));
    }

    String format(String key, Object... arguments) {
        return String.format(locale, get(key), arguments);
    }
}
