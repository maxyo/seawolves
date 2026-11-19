import org.stormengine.port.ResourceImporter;

import java.io.*;
import java.nio.file.*;

/** Publication transaction: replacement, failure/cancel preservation, process death, recovery, locking. */
public class ResourceImporterTest {
    static void writeGame(File root, String marker, String extra) throws IOException {
        for (String name :
                new String[] {"engine.ini", "PROGRAM/code.c", "RESOURCE/data.txt", extra}) {
            File file = new File(root, name);
            if (!file.getParentFile().isDirectory() && !file.getParentFile().mkdirs())
                throw new IOException("mkdirs");
            Files.write(file.toPath(), marker.getBytes("UTF-8"));
        }
    }

    /** A producer that stages a game file by file, reporting progress and honouring cancellation. */
    static int install(File game, String marker, String extra, ResourceImporter.Monitor monitor)
            throws IOException {
        return ResourceImporter.importExtracted(
                (stage, progress) -> {
                    if (!stage.isAbsolute() || !stage.isDirectory() || stage.list().length != 0)
                        throw new AssertionError("Producer did not receive an empty private stage");
                    int files = 0;
                    for (String name :
                            new String[] {
                                "engine.ini", "PROGRAM/code.c", "RESOURCE/data.txt", extra
                            }) {
                        if (progress != null && progress.cancelled())
                            throw new ResourceImporter.CancelledException();
                        File file = new File(stage, name);
                        file.getParentFile().mkdirs();
                        Files.write(file.toPath(), marker.getBytes("UTF-8"));
                        if (progress != null) progress.progress(++files, files);
                    }
                    return files;
                },
                game,
                monitor);
    }

    static File sibling(File game, String suffix) {
        return new File(game.getParentFile(), ".game-import-" + suffix);
    }

    static void assertOld(File game) throws Exception {
        ResourceImporter.requirePublished(game);
        ResourceImporter.validate(game);
        if (!Files.readString(new File(game, "engine.ini").toPath()).equals("old"))
            throw new AssertionError("Previous install changed");
        if (new File(game, "new-only.txt").exists()) throw new AssertionError("Mixed installation");
    }

    static byte[] treeHash(File root) throws Exception {
        java.security.MessageDigest hash = java.security.MessageDigest.getInstance("SHA-256");
        try (java.util.stream.Stream<Path> paths = Files.walk(root.toPath())) {
            for (Path path : (Iterable<Path>) paths.sorted()::iterator) {
                hash.update(root.toPath().relativize(path).toString().getBytes("UTF-8"));
                if (Files.isRegularFile(path)) hash.update(Files.readAllBytes(path));
            }
        }
        return hash.digest();
    }

    static void transactions(File parent) throws Exception {
        File game = new File(parent, "game");
        install(game, "old", "old-only.txt", null);
        assertOld(game);
        byte[] old = treeHash(game);
        for (String failure :
                new String[] {"producer", "missing-root", "cancel", "negative-count"}) {
            ResourceImporter.Monitor monitor =
                    new ResourceImporter.Monitor() {
                        boolean cancelled;

                        public boolean cancelled() {
                            return cancelled;
                        }

                        public void progress(int files, long bytes) {
                            cancelled = failure.equals("cancel");
                        }
                    };
            try {
                ResourceImporter.importExtracted(
                        (stage, progress) -> {
                            if (failure.equals("missing-root")) {
                                Files.write(new File(stage, "engine.ini").toPath(), new byte[] {7});
                                return 1;
                            }
                            writeGame(stage, "new", "new-only.txt");
                            progress.progress(4, 12);
                            if (failure.equals("producer"))
                                throw new IOException("Synthetic producer failed after writing");
                            if (progress.cancelled())
                                throw new ResourceImporter.CancelledException();
                            return failure.equals("negative-count") ? -1 : 4;
                        },
                        game,
                        monitor);
                throw new AssertionError("Failed/cancelled stage published: " + failure);
            } catch (IOException expected) {
                if (failure.equals("cancel")
                        && !(expected instanceof ResourceImporter.CancelledException))
                    throw expected;
                ResourceImporter.requirePublished(game);
                if (!java.util.Arrays.equals(old, treeHash(game)))
                    throw new AssertionError("Old tree bytes changed: " + failure);
                if (sibling(game, "stage").exists())
                    throw new AssertionError("Failed stage retained: " + failure);
            }
        }
        // A process interrupted while writing journal intent has not moved any live tree yet.
        Files.writeString(sibling(game, "journal-temp").toPath(), "");
        ResourceImporter.recover(game);
        assertOld(game);
        // Kill an actual JVM after extraction has started; its OS lock must release and live data
        // remain intact.
        Process child =
                new ProcessBuilder(
                                new File(System.getProperty("java.home"), "bin/java").getPath(),
                                "-cp",
                                System.getProperty("java.class.path"),
                                "ResourceImporterTest",
                                "crash",
                                game.getPath())
                        .inheritIO()
                        .start();
        if (child.waitFor() != 17) throw new AssertionError("Crash fixture failed");
        assertOld(game);
        ResourceImporter.recover(game);
        assertOld(game);
        // Interrupted directory swap: both old backup and newly published root exist, journal still
        // pending.
        if (!game.renameTo(sibling(game, "backup"))) throw new AssertionError();
        writeGame(game, "new", "new-only.txt");
        Files.writeString(sibling(game, "pending").toPath(), "1");
        try {
            ResourceImporter.requirePublished(game);
            throw new AssertionError("Pending game launch allowed");
        } catch (IOException expected) {
        }
        ResourceImporter.recover(game);
        assertOld(game);
        // Interruption between moving old aside and publishing new.
        if (!game.renameTo(sibling(game, "backup"))) throw new AssertionError();
        Files.writeString(sibling(game, "pending").toPath(), "1");
        ResourceImporter.recover(game);
        assertOld(game);
        // Do not recover the old tree after commit, even if old-tree cleanup was interrupted.
        if (!game.renameTo(sibling(game, "backup"))) throw new AssertionError();
        writeGame(game, "new", "new-only.txt");
        ResourceImporter.recover(game);
        if (!Files.readString(new File(game, "engine.ini").toPath()).equals("new"))
            throw new AssertionError("Committed install rolled back");
        install(game, "old", "old-only.txt", null);
        assertOld(game);
        // Stale private copies and scratch of an interrupted source import are removed.
        for (String name : new String[] {"input", "scratch"}) {
            File stale = sibling(game, name);
            if (!stale.mkdir() || !new File(stale, "nested").mkdir()) throw new AssertionError();
            Files.writeString(new File(stale, "nested/part.bin").toPath(), "x");
        }
        ResourceImporter.recover(game);
        if (sibling(game, "input").exists() || sibling(game, "scratch").exists())
            throw new AssertionError("Recovery kept stale source copies");
        java.util.concurrent.CountDownLatch entered = new java.util.concurrent.CountDownLatch(1);
        java.util.concurrent.CountDownLatch release = new java.util.concurrent.CountDownLatch(1);
        java.util.concurrent.atomic.AtomicReference<Throwable> failure =
                new java.util.concurrent.atomic.AtomicReference<>();
        Thread first =
                new Thread(
                        () -> {
                            try {
                                install(
                                        game,
                                        "new",
                                        "new-only.txt",
                                        new ResourceImporter.Monitor() {
                                            public boolean cancelled() {
                                                return false;
                                            }

                                            public void progress(int files, long bytes) {
                                                entered.countDown();
                                                try {
                                                    release.await();
                                                } catch (InterruptedException e) {
                                                    throw new RuntimeException(e);
                                                }
                                            }
                                        });
                            } catch (Throwable e) {
                                failure.set(e);
                            }
                        });
        first.start();
        if (!entered.await(5, java.util.concurrent.TimeUnit.SECONDS))
            throw new AssertionError("Import did not start");
        try {
            install(game, "other", "other.txt", null);
            throw new AssertionError("Concurrent import accepted");
        } catch (IOException expected) {
            if (!expected.getMessage().contains("already running")) throw expected;
        } finally {
            release.countDown();
            first.join();
        }
        if (failure.get() != null) throw new AssertionError(failure.get());
        if (new File(game, "old-only.txt").exists())
            throw new AssertionError("Replacement retained stale files");
        File initial = new File(parent, "initial/game");
        try {
            ResourceImporter.importExtracted(
                    (stage, progress) -> {
                        throw new IOException("Bad source");
                    },
                    initial,
                    null);
            throw new AssertionError("Bad initial import succeeded");
        } catch (IOException expected) {
            if (initial.exists()) throw new AssertionError("Incomplete initial import published");
        }
        writeGame(initial, "new", "new-only.txt");
        Files.writeString(sibling(initial, "pending").toPath(), "0");
        ResourceImporter.recover(initial);
        if (initial.exists())
            throw new AssertionError("Uncommitted initial publication recovered as valid");
    }

    static void stagesAndInode(File parent) throws Exception {
        File game = new File(parent, "game");
        install(game, "old", "old-only.txt", null);
        byte[] old = treeHash(game);
        // Generic producers enter the same pending recovery before seeing their empty stage.
        if (!game.renameTo(sibling(game, "backup"))) throw new AssertionError();
        Files.writeString(sibling(game, "pending").toPath(), "1");
        final Object[] inode = {null};
        java.util.List<ResourceImporter.Stage> stages = new java.util.ArrayList<>();
        int installed =
                ResourceImporter.importExtracted(
                        (stage, progress) -> {
                            try {
                                if (!java.util.Arrays.equals(old, treeHash(game)))
                                    throw new AssertionError("Producer ran before recovery");
                                writeGame(stage, "new", "new-only.txt");
                                inode[0] =
                                        Files.readAttributes(
                                                        new File(stage, "RESOURCE/data.txt")
                                                                .toPath(),
                                                        java.nio.file.attribute.BasicFileAttributes
                                                                .class)
                                                .fileKey();
                                return 4;
                            } catch (IOException error) {
                                throw error;
                            } catch (Exception error) {
                                throw new IOException(error);
                            }
                        },
                        game,
                        new ResourceImporter.Monitor() {
                            public boolean cancelled() {
                                return false;
                            }

                            public void progress(int files, long bytes) {}

                            public void stage(ResourceImporter.Stage stage, long total) {
                                stages.add(stage);
                            }
                        });
        if (installed != 4
                || new File(game, "old-only.txt").exists()
                || !new File(game, "new-only.txt").isFile())
            throw new AssertionError("Stage publication merged or lost data");
        if (!stages.equals(
                java.util.Arrays.asList(
                        ResourceImporter.Stage.VERIFYING, ResourceImporter.Stage.FINISHING)))
            throw new AssertionError("Publication stages " + stages);
        Object publishedInode =
                Files.readAttributes(
                                new File(game, "RESOURCE/data.txt").toPath(),
                                java.nio.file.attribute.BasicFileAttributes.class)
                        .fileKey();
        if (inode[0] != null && !inode[0].equals(publishedInode))
            throw new AssertionError("Staged data copied instead of renamed");
        ResourceImporter.validate(game);
    }

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && args[0].equals("crash")) {
            install(
                    new File(args[1]),
                    "new",
                    "new-only.txt",
                    new ResourceImporter.Monitor() {
                        public boolean cancelled() {
                            return false;
                        }

                        public void progress(int files, long bytes) {
                            Runtime.getRuntime().halt(17);
                        }
                    });
            throw new AssertionError("Crash fixture did not interrupt extraction");
        }
        File root = Files.createTempDirectory("storm-import-test").toFile();
        try {
            transactions(new File(root, "transactions"));
            stagesAndInode(new File(root, "stages"));
            System.out.println(
                    "PASS: stage transaction replacement, hash preservation on"
                        + " failure/cancel/missing roots, inode-preserving publication,"
                        + " verify/finish stages, process death, recovery of journals, input and"
                        + " scratch, concurrency, initial import");
        } finally {
            try (java.util.stream.Stream<Path> paths = Files.walk(root.toPath())) {
                paths.sorted(java.util.Comparator.reverseOrder())
                        .forEach(
                                p -> {
                                    try {
                                        Files.delete(p);
                                    } catch (IOException e) {
                                        throw new RuntimeException(e);
                                    }
                                });
            }
        }
    }
}
