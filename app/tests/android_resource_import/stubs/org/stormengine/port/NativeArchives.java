package org.stormengine.port;

/** The archive reader as ResourceImporter uses it: tree removal only (HostTreeRemoval). */
final class NativeArchives {
    private NativeArchives() {}

    static String[] removeTree(String path) {
        return HostTreeRemoval.removeTree(path);
    }
}
