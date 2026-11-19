package org.stormengine.port;

import java.io.File;
import java.io.FileNotFoundException;
import java.nio.file.Files;

/** Exercises the path policy called by the actual Android provider; no Android method is invoked. */
public final class ShareSheetPathCheck {
    private static void refused(File cache, String kind, String name) throws Exception {
        try {
            ShareSheet.sharedFile(cache, kind, name);
        } catch (FileNotFoundException expected) {
            return;
        }
        throw new AssertionError("provider accepted " + kind + "/" + name);
    }

    public static void main(String[] args) throws Exception {
        File cache = Files.createTempDirectory("share-provider-").toFile();
        File saves = new File(cache, "saves");
        saves.mkdir();
        File export = new File(saves, "save.zip");
        Files.write(export.toPath(), new byte[] {1, 2, 3});
        if (!ShareSheet.sharedFile(cache, "saves", "save.zip").equals(export))
            throw new AssertionError("valid save URI");
        File diagnostics = new File(cache, "diagnostics");
        diagnostics.mkdir();
        File log = new File(diagnostics, "log.txt");
        Files.write(log.toPath(), new byte[] {4});
        if (!ShareSheet.sharedFile(cache, "diagnostics", "log.txt").equals(log))
            throw new AssertionError("diagnostics URI regressed");
        for (String name :
                new String[] {
                    "", ".", "..", "/save.zip", "../save.zip", "x/y", "x\\y", "missing.zip"
                }) refused(cache, "saves", name);
        refused(cache, "unknown", "save.zip");
        refused(cache, "../saves", "save.zip");
        refused(cache, "saves", null);
        Files.createSymbolicLink(new File(saves, "link.zip").toPath(), log.toPath());
        refused(cache, "saves", "link.zip");
        File linkedCache = Files.createTempDirectory("share-provider-linked-").toFile();
        Files.createSymbolicLink(new File(linkedCache, "saves").toPath(), saves.toPath());
        refused(linkedCache, "saves", "save.zip");
        System.out.println(
                "PASS provider path policy: save/diagnostics exact files; traversal, wrong kind and"
                        + " symlink files/folders refused");
    }
}
