package org.stormengine.port;

import android.content.Context;
import android.content.res.Configuration;
import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** Texts the Java host shows itself, from the UI pack's strings: in game in the running game's language, elsewhere
 *  in the app's language ({@link #appLanguage}: the installed game's, the phone's while none is installed). */
final class MenuText {
    private static final HashMap<String, JSONObject> tables = new HashMap<>();
    private final Context context;
    final String language;

    MenuText(Context context, String gameLanguage) {
        this.context = context;
        Locale locale =
                gameLanguage == null || gameLanguage.trim().isEmpty()
                        ? contextLocale(context)
                        : Locale.forLanguageTag(GameLanguage.code(gameLanguage));
        language = locale.getLanguage();
    }

    private static Locale contextLocale(Context context) {
        Configuration configuration = context.getResources().getConfiguration();
        return configuration.getLocales().isEmpty()
                ? Locale.getDefault()
                : configuration.getLocales().get(0);
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
        } catch (IOException noGame) {
            /* no ready game copy (setup, import, recovery): the phone's language */
        }
        String game = null;
        StringBuilder problem = new StringBuilder();
        if (path != null) {
            try {
                game = GameLanguage.of(new File(path));
            } catch (IOException error) {
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
        } catch (IOException error) {
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

    /** The UI pack's strings of `language` (assets/ui/strings/<code>.json); English must exist. */
    private static synchronized JSONObject load(Context context, String language) {
        JSONObject cached = tables.get(language);
        if (cached != null) return cached;
        JSONObject result = new JSONObject();
        if (language.matches("[a-z]{2,3}")) {
            try {
                byte[] bytes =
                        Streams.read(
                                context.getAssets().open("ui/strings/" + language + ".json"),
                                65536);
                result = new JSONObject(new String(bytes, StandardCharsets.UTF_8));
            } catch (IOException | JSONException error) {
                if (language.equals("en"))
                    throw new IllegalStateException("Missing English UI strings", error);
                // Logged once: the result is cached.
                Log.w(
                        "StormLanguage",
                        "No usable UI strings for " + language + ": English is shown",
                        error);
            }
        }
        tables.put(language, result);
        return result;
    }

    /** A string of the UI pack (mobile-ui/design/strings) in this text's language, English when that language has
     *  none; an absent key is an error (the pack's build checks that every language has every key). */
    String get(String key) {
        for (String code : new String[] {language, "en"}) {
            JSONObject table = load(context, code);
            if (table.has(key)) return table.optString(key);
        }
        throw new IllegalStateException("UI pack string " + key + " is missing");
    }

    /** {@link #get} with {1}, {2}, … replaced by the arguments. */
    String format(String key, String... arguments) {
        String text = get(key);
        for (int i = 0; i < arguments.length; i++)
            text = text.replace("{" + (i + 1) + "}", arguments[i]);
        return text;
    }
}
