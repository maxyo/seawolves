package org.stormengine.port;

import java.io.File;
import java.nio.channels.FileChannel;
import java.util.concurrent.TimeUnit;

/**
 * ResourceImporter.holdImport / importing across processes: another process sees an import while the importing
 * process holds the lock, and nothing once it ends, also when it is killed (the kernel drops the lock).
 */
public final class ImportLockCheck {
    public static void main(String[] args) throws Exception {
        if (args.length == 2) { // the importing process: hold the lock until killed
            FileChannel held = ResourceImporter.holdImport(new File(args[1]));
            System.out.println("held");
            System.out.flush();
            Thread.sleep(60_000);
            held.close();
            return;
        }
        File game = new File(args[0], "game");
        assert !ResourceImporter.importing(game) : "no import before any";
        try (FileChannel own = ResourceImporter.holdImport(game)) {
            assert ResourceImporter.importing(game) : "an import of this process is seen";
        }
        assert !ResourceImporter.importing(game) : "a finished import is gone";
        Process importer =
                new ProcessBuilder(
                                System.getProperty("java.home") + "/bin/java",
                                "-cp",
                                System.getProperty("java.class.path"),
                                ImportLockCheck.class.getName(),
                                "hold",
                                game.getPath())
                        .redirectErrorStream(true)
                        .start();
        assert new java.io.BufferedReader(new java.io.InputStreamReader(importer.getInputStream()))
                .readLine()
                .equals("held");
        assert ResourceImporter.importing(game) : "another process's import is seen";
        importer.destroyForcibly();
        assert importer.waitFor(10, TimeUnit.SECONDS);
        assert !ResourceImporter.importing(game) : "a killed import leaves no stale state";
        System.out.println(
                "PASS: the import lock is seen across processes and dies with its process");
    }
}
