package org.stormengine.port;

import java.io.IOException;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.HashSet;
import java.util.Set;

/**
 * The host stand-in of NativeArchives.removeTree for the checks' NativeArchives: java.nio.file of the host JDK keeps
 * the bytes of each name, so it removes names that are not UTF-8 as the reader does; links are not followed.
 * Existing `refused` paths fail as an unremovable tree would.
 */
public final class HostTreeRemoval {
    public static final Set<String> refused = new HashSet<>();

    private HostTreeRemoval() {}

    static String[] removeTree(String path) {
        Path root = Paths.get(path);
        if (!Files.exists(root, LinkOption.NOFOLLOW_LINKS)) return new String[] {"0", ""};
        if (refused.contains(path))
            return new String[] {"1", "Remove " + path + ": refused by the check"};
        try {
            Files.walkFileTree(
                    root,
                    new SimpleFileVisitor<Path>() {
                        @Override
                        public FileVisitResult visitFile(Path file, BasicFileAttributes attributes)
                                throws IOException {
                            Files.delete(file);
                            return FileVisitResult.CONTINUE;
                        }

                        @Override
                        public FileVisitResult postVisitDirectory(
                                Path directory, IOException failure) throws IOException {
                            if (failure != null) throw failure;
                            Files.delete(directory);
                            return FileVisitResult.CONTINUE;
                        }
                    });
            return new String[] {"0", ""};
        } catch (IOException failure) {
            return new String[] {"1", "Remove " + path + ": " + failure};
        }
    }
}
