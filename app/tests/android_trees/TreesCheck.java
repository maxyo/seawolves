package org.stormengine.port;

import java.io.File;
import java.nio.file.Files;

/** Trees: a symbolic link inside a deleted or measured tree, or the tree's root itself, is never followed. */
public final class TreesCheck {
    public static void main(String[] args) throws Exception {
        File work = new File(args[0]);
        File outside = new File(work, "outside");
        assert outside.mkdirs();
        Files.write(new File(outside, "kept.bin").toPath(), new byte[100]);

        File tree = new File(work, "tree");
        File nested = new File(tree, "a/b");
        assert nested.mkdirs();
        Files.write(new File(nested, "payload.bin").toPath(), new byte[10]);
        Files.write(new File(tree, "top.bin").toPath(), new byte[5]);
        Files.createSymbolicLink(new File(nested, "escape").toPath(), outside.toPath());
        Files.createSymbolicLink(
                new File(tree, "file-link").toPath(), new File(outside, "kept.bin").toPath());

        assert Trees.size(tree) == 15 : "a link is not measured through: " + Trees.size(tree);
        assert Trees.delete(tree) == 15 : "only the tree's own files are counted";
        assert !tree.exists() && !Files.isSymbolicLink(new File(nested, "escape").toPath());
        assert new File(outside, "kept.bin").length() == 100
                : "the link's target directory keeps its contents";

        File link = new File(work, "root-link");
        Files.createSymbolicLink(link.toPath(), outside.toPath());
        assert Trees.size(link) == 0 && Trees.delete(link) == 0;
        assert !Files.isSymbolicLink(link.toPath()) && new File(outside, "kept.bin").isFile()
                : "a root that is a link is removed as itself";

        assert Trees.delete(new File(work, "missing")) == 0
                && Trees.size(new File(work, "missing")) == 0;
        System.out.println(
                "PASS: Trees deletes and measures without following links (inside the tree or as its root)");
    }
}
