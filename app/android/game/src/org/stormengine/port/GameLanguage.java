package org.stormengine.port;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.Set;

/**
 * The language of the app's own screens and texts: the installed game's ([COMMON] defaultLanguage of its
 * language.ini), so the app speaks as the game does; the phone's while no game is installed. Languages are game
 * language names in lower case ("russian"); the texts are chosen by ISO 639 code ({@link #code}).
 */
final class GameLanguage {
    private GameLanguage() {}

    /** The game copy's language, or null when it names none (a broken or foreign copy) or cannot be read. */
    static String of(File gameRoot) throws IOException {
        File ini = find(gameRoot, new String[] {"RESOURCE", "INI", "TEXTS", "language.ini"});
        if (ini == null) return null;
        try (BufferedReader reader =
                new BufferedReader(
                        new InputStreamReader(
                                new FileInputStream(ini), StandardCharsets.ISO_8859_1))) {
            boolean common = false;
            for (String line; (line = reader.readLine()) != null; ) {
                line = line.replace("ï»¿", "").trim(); // a UTF-8 byte order mark, read as Latin-1
                if (line.startsWith("[")) {
                    common = line.equalsIgnoreCase("[COMMON]");
                    continue;
                }
                int equals = line.indexOf('=');
                if (!common
                        || equals < 0
                        || !line.substring(0, equals).trim().equalsIgnoreCase("defaultLanguage"))
                    continue;
                String value = line.substring(equals + 1);
                int comment = value.indexOf(';');
                if (comment >= 0) value = value.substring(0, comment);
                value = value.trim().toLowerCase(Locale.ROOT);
                return value.isEmpty() ? null : value;
            }
        }
        return null;
    }

    /**
     * The language of the app's screens. installed: a game copy is installed and ready; game: its language
     * ({@link #of}), null when it names none; texts: the ISO codes the app has texts for; phone: the phone's
     * language as a game language name. A game whose language is missing or that the app has no texts for
     * leaves the phone's language, and `problem` gets why, which the caller logs as an error.
     */
    static String choose(
            boolean installed,
            String game,
            Set<String> texts,
            String phone,
            StringBuilder problem) {
        if (!installed) return phone;
        if (game == null) {
            problem.append(
                            "the installed game names no [COMMON] defaultLanguage in RESOURCE/INI/TEXTS/language.ini; the app's screens use the phone's language (")
                    .append(phone)
                    .append(')');
            return phone;
        }
        if (!texts.contains(code(game))) {
            problem.append("the installed game's language '")
                    .append(game)
                    .append("' has no texts of the app (")
                    .append(texts)
                    .append("); the app's screens use the phone's language (")
                    .append(phone)
                    .append(')');
            return phone;
        }
        return game;
    }

    /** The ISO 639 code of a game language name ("russian" → "ru"); another name is read as a language tag. */
    static String code(String name) {
        if (name == null || name.trim().isEmpty()) return Locale.getDefault().getLanguage();
        name = name.trim();
        switch (name.toLowerCase(Locale.ROOT)) {
            case "russian":
                return "ru";
            case "english":
                return "en";
            case "german":
                return "de";
            case "french":
                return "fr";
            case "spanish":
                return "es";
            case "italian":
                return "it";
            case "polish":
                return "pl";
            case "ukrainian":
                return "uk";
            default:
                return Locale.forLanguageTag(name.replace('_', '-')).getLanguage();
        }
    }

    /** The game language name of an ISO 639 code ("ru" → "russian"); a code without one is returned as it is
     *  (the UI then reports a language without tables and uses English). */
    static String name(String code) {
        switch (code == null ? "" : code) {
            case "ru":
                return "russian";
            case "en":
                return "english";
            case "de":
                return "german";
            case "fr":
                return "french";
            case "es":
                return "spanish";
            case "it":
                return "italian";
            case "pl":
                return "polish";
            case "uk":
                return "ukrainian";
            default:
                return code == null ? "" : code;
        }
    }

    private static File find(File base, String[] parts) {
        File current = base;
        for (String part : parts) {
            File[] children = current == null ? null : current.listFiles();
            File match = null;
            if (children != null)
                for (File child : children)
                    if (child.getName().equalsIgnoreCase(part)) match = child;
            if (match == null) return null;
            current = match;
        }
        return current.isFile() ? current : null;
    }
}
