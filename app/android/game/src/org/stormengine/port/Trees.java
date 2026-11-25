package org.stormengine.port;

import java.io.File;
import java.io.IOException;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;

/**
 * Trees whose names the app or the engine wrote (the engine cache, the installed UI layers), walked and deleted
 * without following symbolic links: a link is counted and removed as itself, its target is never touched. A tree of
 * untrusted names (an import stage, a download, anything from an archive or a torrent) is removed by the bytes of its
 * names instead, which java.nio.file cannot do for a name that is not UTF-8: ResourceImporter.deleteTree.
 */
final class Trees {
    private Trees() {}

    /** Deletes {@code root} and everything under it; returns the bytes of the regular files removed (0: missing). */
    static long delete(File root) throws IOException {
        final long[] bytes = {0};
        walk(
                root,
                new SimpleFileVisitor<Path>() {
                    @Override
                    public FileVisitResult visitFile(Path file, BasicFileAttributes attributes)
                            throws IOException {
                        if (attributes.isRegularFile()) bytes[0] += attributes.size();
                        Files.delete(file);
                        return FileVisitResult.CONTINUE;
                    }

                    @Override
                    public FileVisitResult postVisitDirectory(Path directory, IOException failed)
                            throws IOException {
                        if (failed != null) throw failed;
                        Files.delete(directory);
                        return FileVisitResult.CONTINUE;
                    }
                });
        return bytes[0];
    }

    /** Bytes of the regular files under {@code root} (0: missing). */
    static long size(File root) throws IOException {
        final long[] bytes = {0};
        walk(
                root,
                new SimpleFileVisitor<Path>() {
                    @Override
                    public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) {
                        if (attributes.isRegularFile()) bytes[0] += attributes.size();
                        return FileVisitResult.CONTINUE;
                    }
                });
        return bytes[0];
    }

    /** Files.walkFileTree without FOLLOW_LINKS; a failure to read an entry is thrown (SimpleFileVisitor). */
    private static void walk(File root, SimpleFileVisitor<Path> visitor) throws IOException {
        Path path = root.toPath();
        if (Files.exists(path, LinkOption.NOFOLLOW_LINKS)) Files.walkFileTree(path, visitor);
    }
}
