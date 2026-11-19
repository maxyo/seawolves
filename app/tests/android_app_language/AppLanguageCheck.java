package org.stormengine.port;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/** The language of the app's screens: the installed game's, the phone's without a game, a stated exception otherwise. */
public final class AppLanguageCheck {
    public static void main(String[] args) throws IOException {
        // The installed game's own language: language.ini [COMMON] defaultLanguage, any case of
        // path and key.
        File root = Files.createTempDirectory("storm-language").toFile();
        File texts = new File(root, "Resource/INI/texts");
        if (!texts.mkdirs()) throw new IOException("mkdirs");
        write(
                new File(texts, "Language.ini"),
                "ï»¿[COMMON]\r\n"
                        + "GlobalFile = common.ini\r\n"
                        + " defaultLanguage = Russian ; the edition\r\n"
                        + "[Russian]\r\n"
                        + "defaultLanguage = English\r\n");
        assert "russian".equals(GameLanguage.of(root)) : GameLanguage.of(root);
        write(new File(texts, "Language.ini"), "[COMMON]\nGlobalFile = common.ini\n");
        assert GameLanguage.of(root) == null : "no defaultLanguage: none";
        assert GameLanguage.of(new File(root, "missing")) == null : "no game copy: none";

        Set<String> pack = new HashSet<>(Arrays.asList("en", "ru", "de", "fr", "es", "it"));
        StringBuilder problem = new StringBuilder();
        // No game installed yet (install and download screens): the phone's language.
        assert "english".equals(GameLanguage.choose(false, null, pack, "english", problem))
                && problem.length() == 0;
        assert "german".equals(GameLanguage.choose(false, "russian", pack, "german", problem))
                        && problem.length() == 0
                : "a game that is not ready yet does not decide";
        // A game is installed: its language, whatever the phone's.
        assert "russian".equals(GameLanguage.choose(true, "russian", pack, "english", problem))
                && problem.length() == 0;
        assert "english".equals(GameLanguage.choose(true, "english", pack, "russian", problem))
                && problem.length() == 0;
        // An installed game the app cannot follow: the phone's language, and why is said (logged as
        // an error).
        assert "english".equals(GameLanguage.choose(true, "polish", pack, "english", problem))
                : "no Polish strings";
        assert problem.toString().contains("'polish' has no texts of the app") : problem;
        problem.setLength(0);
        assert "russian".equals(GameLanguage.choose(true, null, pack, "russian", problem));
        assert problem.toString().contains("names no [COMMON] defaultLanguage") : problem;

        // Names and codes both ways.
        assert GameLanguage.code("Russian").equals("ru")
                && GameLanguage.code("italian").equals("it")
                && GameLanguage.code("pt_BR").equals("pt");
        assert GameLanguage.name("ru").equals("russian")
                && GameLanguage.name("es").equals("spanish")
                && GameLanguage.name("ja").equals("ja");
        System.out.println(
                "PASS: app language: the installed game's (language.ini), the phone's without a"
                        + " game, stated when unsupported");
    }

    private static void write(File file, String text) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.ISO_8859_1));
        }
    }
}
