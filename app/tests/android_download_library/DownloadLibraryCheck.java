package org.stormengine.port;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.*;

public final class DownloadLibraryCheck {
    interface Checked {
        void run() throws Exception;
    }

    static void rejected(Checked action) throws Exception {
        try {
            action.run();
            throw new AssertionError("Operation should be rejected");
        } catch (IOException expected) {
        }
    }

    static void select(GameDownloads.Entry e, int... indices) throws Exception {
        TorrentStatus.prepareSelection(selectionCatalog, e, indices);
    }

    static GameDownloads selectionCatalog;

    static JSONObject file(int index, String path, long size, boolean selectable)
            throws JSONException {
        return new JSONObject()
                .put("index", index)
                .put("path", path)
                .put("bytes", size)
                .put("selectable", selectable);
    }

    public static void main(String[] args) throws Exception {
        File root = new File(args[0]);
        GameDownloads catalog = new GameDownloads(root);
        List<String> names =
                Arrays.asList("setup.exe", "setup-1a.bin", "setup-1b.bin", "setup-1c.bin");
        List<String> uris =
                Arrays.asList(
                        "content://test/1",
                        "content://test/2",
                        "content://test/3",
                        "content://test/4");
        GameDownloads.Entry first =
                catalog.create("archive", "Игра \"Жемчуг\" 🏴\n", "setup.exe", names, uris);
        GameDownloads.Entry second =
                catalog.create("archive", first.name, "setup.exe", names, uris);
        assert !first.id.equals(second.id)
                && !catalog.directory(first.id).equals(catalog.directory(second.id));
        String firstMetadata = "{\"source\":\"a\\\\b\",\"line\":\"x\\ny\"}";
        assert !first.hasMetadata
                && catalog.metadata(first).isEmpty()
                && catalog.selection(first).length == 0;
        first.phase = "payload";
        catalog.saveMetadata(first, firstMetadata);
        catalog.saveSelection(first, new int[] {8});
        first.downloaded = 7044144197L;
        first.wanted = 9999999999L;
        first.state = "downloading";
        catalog.save(first);
        GameDownloads reloaded = new GameDownloads(root);
        GameDownloads.Entry copy = reloaded.read(first.id);
        assert copy.name.equals(first.name)
                && copy.hasMetadata
                && reloaded.metadata(copy).equals(firstMetadata)
                && copy.downloaded == 7044144197L;
        assert copy.selectedCount == 1
                && reloaded.selection(copy)[0] == 8
                && copy.phase.equals("payload")
                && copy.documents.equals(uris);
        assert !new String(
                                Files.readAllBytes(
                                        new File(catalog.directory(first.id), "set.json").toPath()),
                                StandardCharsets.UTF_8)
                        .contains("source\\")
                : "the file list is kept outside the status record";
        File record = new File(catalog.directory(first.id), "set.json");
        File unfinished = new File(record.getParentFile(), "set.json.tmp");
        Files.write(
                unfinished.toPath(), "broken interrupted write".getBytes(StandardCharsets.UTF_8));
        assert reloaded.read(first.id).downloaded == first.downloaded
                : "unfinished atomic replacement must not hide published record";
        reloaded.recoverInterrupted();
        copy = reloaded.read(first.id);
        assert copy.state.equals("interrupted")
                        && copy.phase.equals("payload")
                        && reloaded.selection(copy)[0] == 8
                        && copy.error.isEmpty()
                        && copy.errorKind.isEmpty()
                : "an interrupted set is a state, never an error without a kind";
        assert copy.downloaded == first.downloaded
                : "restart retains real bytes and selection, never auto-resumes";
        rejected(() -> catalog.directory("../active-game"));
        rejected(() -> catalog.input(first, "../engine.ini"));
        rejected(() -> catalog.input(first, "bad\\name.bin"));
        rejected(
                () ->
                        catalog.create(
                                "archive",
                                "duplicate",
                                "A.bin",
                                Arrays.asList("A.bin", "a.BIN"),
                                Arrays.asList("a", "b")));
        magnetChecks(catalog);
        // A link in a set (a torrent's payload may carry one) is removed with it, never followed.
        File outside = new File(root.getParentFile(), "active-game");
        File sentinel = new File(outside, "sentinel");
        assert outside.mkdirs();
        Files.write(sentinel.toPath(), new byte[] {42});
        Files.createSymbolicLink(
                new File(catalog.directory(second.id), "unsafe-link").toPath(), outside.toPath());
        catalog.remove(second.id);
        assert !catalog.directory(second.id).exists() && sentinel.length() == 1;

        // Readers must see one complete JSON record while separate catalog objects save.
        GameDownloads concurrent = new GameDownloads(root);
        ExecutorService pool = Executors.newFixedThreadPool(2);
        Future<?> writer =
                pool.submit(
                        () -> {
                            try {
                                for (int i = 0; i < 100; i++) {
                                    GameDownloads.Entry e = concurrent.read(first.id);
                                    e.downloaded = i;
                                    concurrent.save(e);
                                }
                            } catch (Exception e) {
                                throw new RuntimeException(e);
                            }
                        });
        Future<?> reader =
                pool.submit(
                        () -> {
                            try {
                                for (int i = 0; i < 100; i++) {
                                    JSONObject disk =
                                            GameDownloads.object(
                                                    new String(
                                                            Files.readAllBytes(record.toPath()),
                                                            StandardCharsets.UTF_8));
                                    assert disk.getInt("selectedCount") == 1;
                                }
                            } catch (Exception e) {
                                throw new RuntimeException(e);
                            }
                        });
        writer.get();
        reader.get();
        pool.shutdown();
        // A record cut short (a write the system lost) is damaged, not a different record.
        Files.write(record.toPath(), "{\"version\":1,\"id\":\"".getBytes(StandardCharsets.UTF_8));
        rejected(() -> catalog.read(first.id));
        assert catalog.list().get(0).corrupt : "damaged record remains visible, no fake readiness";
        catalog.remove(first.id);
        recoveryChecks(root);

        selectionChecks(catalog);
        statusChecks(catalog);
        stateChecks(catalog);
        importSourceChecks(catalog);
        System.out.println(
                "PASS: production catalog JSON roundtrip/Unicode/large bytes, independent IDs, atomic interrupted writes, explicit recovery, path/case/symlink guards, concurrent persistence, corruption visibility, deletion isolation");
    }

    static void magnetChecks(GameDownloads catalog) throws Exception {
        for (String bad :
                new String[] {
                    "https://not-a-magnet",
                    "magnet:?dn=no-hash",
                    "magnet:?xt=urn:btih:zz",
                    "magnet:?xt=urn:btih:0123456789abcdef",
                    "magnet:?xt=urn:btmh:1220abcd",
                    "magnet:?xt=urn:btih:" + "g".repeat(40)
                }) {
            try {
                catalog.create(
                        "magnet", "bad", bad, Collections.emptyList(), Collections.emptyList());
                throw new AssertionError("Accepted " + bad);
            } catch (DownloadProblem expected) {
                assert expected.kind.equals(
                                bad.startsWith("magnet:?") ? "magnet-damaged" : "magnet-prefix")
                        : bad + ": " + expected.kind;
            }
        }
        for (String good :
                new String[] {
                    "magnet:?xt=urn:btih:" + "0123456789abcdefABCD".repeat(2) + "&dn=x",
                    "magnet:?dn=y&xt=urn:btih:ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
                    "magnet:?xt=urn:btmh:1220" + "0f".repeat(32)
                })
            catalog.remove(
                    catalog.create(
                                    "magnet",
                                    "ok",
                                    good,
                                    Collections.emptyList(),
                                    Collections.emptyList())
                            .id);
        System.out.println(
                "PASS: magnet links need a well-formed v1 (hex/base32) or v2 info-hash; invalid ones fail with a kind the player is told");
    }

    static final String METADATA_V2 =
            "{\"metadataVersion\":2,\"name\":\"Bundle\",\"totalBytes\":36,\"candidates\":[],\"files\":[{\"index\":0,\"path\":\"Bundle/setup.exe\",\"bytes\":10,\"selectable\":true},{\"index\":1,\"path\":\"Bundle/setup-1.bin\",\"bytes\":20,\"selectable\":true},{\"index\":2,\"path\":\"Bundle/.pad/0\",\"bytes\":5,\"selectable\":false},{\"index\":3,\"path\":\"Bundle/readme.txt\",\"bytes\":1,\"selectable\":true}]}";

    static void selectionChecks(GameDownloads catalog) throws Exception {
        selectionCatalog = catalog;
        GameDownloads.Entry e =
                catalog.create(
                        "magnet",
                        "Set",
                        "magnet:?xt=urn:btih:" + "a".repeat(40),
                        Collections.emptyList(),
                        Collections.emptyList());
        rejected(() -> select(e, 0)); // no file list yet
        catalog.saveMetadata(
                e,
                "{\"candidate\":true,\"files\":[{\"index\":0,\"path\":\"x\",\"bytes\":1,\"selectable\":true}]}");
        rejected(() -> select(e, 0)); // earlier metadata: refresh first
        catalog.saveMetadata(e, METADATA_V2);
        e.sourceState = "complete";
        e.errorKind = "storage-full";
        e.error = "full";
        rejected(() -> select(e));
        rejected(() -> select(e, 2));
        rejected(() -> select(e, 0, 0));
        rejected(() -> select(e, 9));
        rejected(() -> select(e, -1));
        assert e.phase.equals("metadata")
                        && e.selectedCount == 0
                        && catalog.selection(e).length == 0
                : "rejected selections leave the set unchanged";
        select(e, 3, 0, 1);
        assert e.phase.equals("payload")
                && e.selectedCount == 3
                && Arrays.equals(catalog.selection(e), new int[] {3, 0, 1})
                && e.sourceState.isEmpty()
                && e.errorKind.isEmpty()
                && e.error.isEmpty();
        GameDownloads.Entry archive =
                catalog.create(
                        "archive",
                        "a.zip",
                        "a.zip",
                        Arrays.asList("a.zip"),
                        Arrays.asList("content://a"));
        rejected(() -> select(archive, 0));
        catalog.remove(e.id);
        catalog.remove(archive.id);
        System.out.println(
                "PASS: production selection gate: any explicitly chosen selectable file of v2 metadata, never padding, duplicates, unknown indices or earlier metadata");
    }

    static void statusChecks(GameDownloads catalog) throws Exception {
        TorrentStatus status = new TorrentStatus();
        TorrentStatus.FileList fileList = () -> NativeTorrents.metadata(0);
        boolean online = true;
        GameDownloads.Entry e =
                catalog.create(
                        "magnet",
                        "Set",
                        "magnet:?xt=urn:btih:" + "b".repeat(40),
                        Collections.emptyList(),
                        Collections.emptyList());
        status.apply(
                catalog,
                e,
                new JSONObject().put("state", "complete").put("checkpointPending", true),
                online,
                fileList);
        assert e.state.equals("pausing")
                : "checkpoint-pending completion must never look importable";
        status.apply(
                catalog,
                e,
                new JSONObject()
                        .put("state", "downloading")
                        .put("downloadedBytes", 5L)
                        .put("wantedBytes", 9L)
                        .put("bytesPerSecond", 0L)
                        .put("peers", 0L)
                        .put("seeds", 0L)
                        .put("stalledSeconds", 130L)
                        .put("networkError", "Tracker udp://t: host unreachable"),
                online,
                fileList);
        assert e.stalledSeconds == 130
                && e.peers == 0
                && e.networkError.startsWith("Tracker")
                && status.stallLogged;
        status.apply(
                catalog,
                e,
                new JSONObject()
                        .put("state", "downloading")
                        .put("stalledSeconds", 0L)
                        .put("peers", 3L),
                online,
                fileList);
        assert !status.stallLogged && e.peers == 3 && e.networkError.isEmpty();
        assert !e.hasMetadata && NativeTorrents.metadataCalls == 0
                : "no file list is fetched before the session has it";
        status.apply(
                catalog,
                e,
                new JSONObject().put("state", "checking").put("checkProgress", 0.345),
                online,
                fileList);
        assert e.state.equals("checking") && e.checkedPermille == 345 && !e.offline
                : "the hash check of a resume is its own state with its share";
        online = false;
        status.apply(
                catalog,
                e,
                new JSONObject().put("state", "downloading").put("stalledSeconds", 300L),
                online,
                fileList);
        assert e.offline && e.checkedPermille == 0 : "a poll without network says so";
        assert library(e).offline && !library(e).stalled
                : "without network the set is offline, not stalled for want of peers";
        online = true;
        status.apply(
                catalog,
                e,
                new JSONObject().put("state", "downloading").put("stalledSeconds", 0L),
                online,
                fileList);
        assert !e.offline;
        status.apply(
                catalog,
                e,
                new JSONObject().put("state", "awaiting-selection").put("metadataReady", true),
                online,
                fileList);
        status.apply(
                catalog,
                e,
                new JSONObject().put("state", "awaiting-selection").put("metadataReady", true),
                online,
                fileList);
        assert NativeTorrents.metadataCalls == 1
                        && e.hasMetadata
                        && catalog.metadata(e).equals(METADATA_V2)
                        && e.name.equals("Bundle")
                : "the file list is fetched once per session and kept";
        assert TorrentStatus.errorKind(
                        new DownloadProblem("storage-full", "No space left on device"))
                .equals("storage-full");
        assert TorrentStatus.errorKind(new DownloadProblem("magnet-damaged", "x"))
                .equals("magnet-damaged");
        assert TorrentStatus.errorKind(
                                new IOException("Not enough free storage for selected payload"))
                        .equals("failed")
                : "a message is never parsed: no space reaches the app as the transfer's errorKind";
        assert TorrentStatus.errorKind(new IOException("Torrent session is closed"))
                .equals("failed");
        e.errorKind = "storage-full";
        e.networkError = "n";
        e.peers = 4;
        e.seeds = 1;
        e.stalledSeconds = 7;
        e.offline = true;
        e.checkedPermille = 500;
        catalog.save(e);
        GameDownloads.Entry reread = catalog.read(e.id);
        assert reread.errorKind.equals("storage-full")
                && reread.networkError.equals("n")
                && reread.peers == 4
                && reread.seeds == 1
                && reread.stalledSeconds == 7
                && reread.offline
                && reread.checkedPermille == 500;
        catalog.remove(e.id);
        System.out.println(
                "PASS: production status mapping: pending checkpoint, peers/seeds, stall episodes logged once, network errors, error kinds (native, storage), persisted health fields");
    }

    static DownloadLibraryState.Row library(GameDownloads.Entry e) {
        return new DownloadLibraryState(x -> "").rows(Arrays.asList(e), false).get(0);
    }

    static final Map<String, String> texts = new HashMap<>();

    static GameDownloads.Entry entry(String state, String sourceState, String metadata) {
        GameDownloads.Entry e = new GameDownloads.Entry();
        e.id = UUID.randomUUID().toString();
        e.kind = "magnet";
        e.name = "Set";
        e.state = state;
        e.sourceState = sourceState;
        e.hasMetadata = !metadata.isEmpty();
        e.metadataStamp = 1;
        texts.put(e.id, metadata);
        return e;
    }

    static List<String> verbs(DownloadLibraryState.Row r) {
        List<String> all = new ArrayList<>();
        if (!r.main.isEmpty()) all.add(r.main);
        all.addAll(r.menu);
        return all;
    }

    static void stateChecks(GameDownloads catalog) throws Exception {
        String meta =
                "{\"metadataVersion\":2,\"name\":\"Game \u00e9\",\"totalBytes\":36,\"candidates\":[{\"id\":\"installer:0\",\"kind\":\"installer-candidate\",\"root\":\"Bundle\",\"indices\":[0],\"companionIndices\":[1]},{\"id\":\"archive:x\",\"kind\":\"archive-candidate\",\"root\":\"Bundle\",\"indices\":[3],\"companionIndices\":[]}],\"files\":[{\"index\":0,\"path\":\"Bundle/setup.exe\",\"bytes\":10,\"selectable\":true},{\"index\":1,\"path\":\"Bundle/setup-1.bin\",\"bytes\":20,\"selectable\":true},{\"index\":2,\"path\":\"Bundle/.pad/0\",\"bytes\":5,\"selectable\":false},{\"index\":3,\"path\":\"Bundle/x.zip\",\"bytes\":1,\"selectable\":true}]}";
        DownloadLibraryState library = new DownloadLibraryState(e -> texts.get(e.id));
        GameDownloads.Entry awaiting = entry("awaiting-selection", "", meta),
                downloading = entry("downloading", "", meta),
                done = entry("complete", "complete", meta),
                interrupted = entry("interrupted", "", meta),
                failed = entry("error", "", ""),
                saved = entry("saved", "", ""),
                corrupt = entry("error", "", "");
        corrupt.corrupt = true;
        downloading.rate = 1000;
        downloading.stalledSeconds = DownloadLibraryState.STALL_SECONDS;
        failed.errorKind = "storage-full";
        failed.error = "No space left on device";
        List<DownloadLibraryState.Row> rows =
                library.rows(
                        Arrays.asList(
                                awaiting, downloading, done, interrupted, failed, saved, corrupt),
                        false);
        assert verbs(rows.get(0)).equals(Arrays.asList("choose", "details", "delete"))
                && rows.get(0).name.equals("Game \u00e9");
        assert verbs(rows.get(1)).equals(Arrays.asList("pause", "details"))
                && rows.get(1).stalled
                && rows.get(1).rate == 1000;
        assert verbs(rows.get(2)).equals(Arrays.asList("import", "choose", "details", "delete"))
                && rows.get(2).ready
                && rows.get(2).state.equals("complete");
        assert verbs(rows.get(3)).equals(Arrays.asList("resume", "choose", "details", "delete"))
                && !rows.get(3).ready;
        assert verbs(rows.get(4)).equals(Arrays.asList("resume", "delete"))
                && rows.get(4).errorKind.equals("storage-full");
        assert rows.get(5).state.equals("interrupted") && rows.get(5).main.equals("resume")
                : "a saved set without metadata is shown as interrupted";
        assert rows.get(6).main.isEmpty()
                && rows.get(6).menu.equals(Arrays.asList("delete"))
                && rows.get(6).state.equals("error");
        assert rows.get(2).discovered.equals("checking")
                        && rows.get(0).discovered.isEmpty()
                        && rows.get(3).discovered.isEmpty()
                : "only a ready set is discovered, and its candidates (file names) claim nothing";
        done.discovered = "none";
        assert library.rows(Arrays.asList(done), false).get(0).discovered.equals("none")
                : "a ready set shows the discovery's own result";
        done.discovered = "";
        downloading.stalledSeconds = DownloadLibraryState.STALL_SECONDS - 1;
        assert !library.rows(Arrays.asList(downloading), false).get(0).stalled;
        List<DownloadLibraryState.Row> blocked =
                library.rows(Arrays.asList(done, interrupted, downloading), true);
        assert verbs(blocked.get(0)).equals(Arrays.asList("details"))
                && blocked.get(0).main.isEmpty()
                && verbs(blocked.get(1)).equals(Arrays.asList("details"));
        assert verbs(blocked.get(2)).equals(Arrays.asList("pause", "details"))
                : "a running import still lets a download pause";
        DownloadLibraryState.Candidate installer = rows.get(0).candidates.get(0);
        assert Arrays.equals(installer.files, new int[] {0, 1})
                && installer.bytes == 30
                && installer.kind.equals("installer-candidate");
        assert Arrays.equals(library.allFiles(awaiting), new int[] {0, 1, 3})
                : "padding is never offered";
        DownloadLibraryState.Metadata first = library.metadata(awaiting);
        texts.put(
                awaiting.id,
                meta.replace("Game \u00e9", "Renamed")); // a refresh with the same file stamp
        assert library.metadata(awaiting) == first : "unchanged metadata reuses its snapshot";
        awaiting.metadataStamp = 2;
        assert library.metadata(awaiting) != first
                && library.metadata(awaiting).name.equals("Renamed");
        GameDownloads.Entry old =
                entry("awaiting-selection", "", "{\"candidate\":true,\"files\":[]}");
        DownloadLibraryState.Row oldRow = library.rows(Arrays.asList(old), false).get(0);
        assert oldRow.candidates.isEmpty() && oldRow.main.equals("resume")
                : "unreadable metadata is fetched again before any choice";
        System.out.println(
                "PASS: production screen state: verbs per state, stall threshold, error kinds, import gate, unknown states, candidates with volume hints (no game claim from names), discovery result of ready sets, padding excluded, metadata snapshots reused until changed");
    }

    static void importSourceChecks(GameDownloads catalog) throws Exception {
        GameDownloads.Entry e =
                catalog.create(
                        "magnet",
                        "Set",
                        "magnet:?xt=urn:btih:" + "c".repeat(40),
                        Collections.emptyList(),
                        Collections.emptyList());
        rejected(() -> catalog.importSource(e.id));
        e.sourceState = "complete";
        e.state = "downloading";
        catalog.save(e);
        rejected(() -> catalog.importSource(e.id)); // still being written
        e.state = "complete";
        catalog.save(e);
        rejected(() -> catalog.importSource(e.id)); // no files yet
        File payload = new File(catalog.sessionDirectory(e), "payload");
        assert payload.mkdir();
        assert catalog.importSource(e.id).equals(payload.getCanonicalFile());
        catalog.recordDiscovery(e.id, "game");
        assert catalog.read(e.id).discovered.equals("game")
                : "the discovery of a ready set is kept";
        GameDownloads.Entry restarted = catalog.read(e.id);
        restarted.sourceState = "";
        restarted.discovered = "";
        restarted.state = "downloading";
        catalog.save(restarted);
        catalog.recordDiscovery(e.id, "game");
        assert catalog.read(e.id).discovered.isEmpty()
                : "a discovery that finishes after the set changed is dropped";
        restarted.state = "complete";
        restarted.sourceState = "complete";
        restarted.discovered = "maybe";
        rejected(
                () -> {
                    catalog.save(restarted);
                    catalog.read(e.id);
                });
        restarted.discovered = "";
        catalog.save(restarted);
        GameDownloads.Entry local =
                catalog.create(
                        "archive",
                        "a.zip",
                        "a.zip",
                        Arrays.asList("a.zip"),
                        Arrays.asList("content://a"));
        Files.write(catalog.input(local, "a.zip").toPath(), new byte[] {1});
        local.state = "complete";
        local.sourceState = "complete";
        catalog.save(local);
        assert catalog.importSource(local.id).equals(catalog.input(local, "a.zip").getParentFile());
        rejected(() -> catalog.importSource("../x"));
        catalog.remove(e.id);
        catalog.remove(local.id);
        System.out.println(
                "PASS: import source: only a finished, checkpointed set, its payload or copied documents, inside the library; its discovery result kept only while the set stays ready");
    }

    static void recoveryChecks(File root) throws Exception {
        GameDownloads beforeUpdate = new GameDownloads(root);
        GameDownloads.Entry e =
                beforeUpdate.create(
                        "archive",
                        "Retained package",
                        "game.zip",
                        Arrays.asList("game.zip"),
                        Arrays.asList("content://test/update"));
        File input = beforeUpdate.input(e, "game.zip");
        byte[] payload = {3, 1, 4, 1, 5};
        Files.write(input.toPath(), payload);
        e.state = "downloading";
        e.rate = 838370;
        beforeUpdate.save(e);
        File record = new File(beforeUpdate.directory(e.id), "set.json");
        // A fresh instance models APK replacement: paths and native resume files stay put.
        File resume = new File(beforeUpdate.sessionDirectory(e), "resume.dat");
        Files.write(resume.toPath(), new byte[] {8, 9});
        GameDownloads afterUpdate = new GameDownloads(root);
        afterUpdate.recoverInterrupted();
        GameDownloads.Entry recovered = afterUpdate.read(e.id);
        assert recovered.state.equals("interrupted") && recovered.rate == 0;
        assert afterUpdate.input(recovered, "game.zip").equals(input)
                && Arrays.equals(payload, Files.readAllBytes(input.toPath()));
        assert Arrays.equals(new byte[] {8, 9}, Files.readAllBytes(resume.toPath()));
        // Unknown schemas and invalid JSON are visible/read-only, never rewritten or removed.
        byte[] supported = Files.readAllBytes(record.toPath());
        byte[][] incompatible = {
            new String(supported, StandardCharsets.UTF_8)
                    .replace("\"version\":1", "\"version\":2")
                    .getBytes(StandardCharsets.UTF_8),
            "{\"version\":1,\"version\":2}".getBytes(StandardCharsets.UTF_8)
        };
        File unknown = new File(root, "future-app-owned-folder");
        assert unknown.mkdir();
        File marker = new File(unknown, "keep.partial");
        Files.write(marker.toPath(), payload);
        for (byte[] bytes : incompatible) {
            Files.write(record.toPath(), bytes);
            GameDownloads rollback = new GameDownloads(root);
            rollback.recoverInterrupted();
            GameDownloads.Entry readonly =
                    rollback.list().stream().filter(item -> item.id.equals(e.id)).findFirst().get();
            assert readonly.corrupt;
            rejected(() -> rollback.save(readonly));
            assert Arrays.equals(bytes, Files.readAllBytes(record.toPath()))
                    : "unknown/invalid catalog must remain byte-for-byte recoverable";
            assert Arrays.equals(payload, Files.readAllBytes(input.toPath()))
                    && resume.isFile()
                    && marker.isFile();
        }
        Files.write(record.toPath(), supported);
        afterUpdate.remove(e.id);
        assert marker.delete() && unknown.delete();
        System.out.println(
                "PASS: catalog recovery: stable input/resume paths, unknown schemas/invalid JSON preserved read-only, no startup deletion of unowned temporary data");
    }
}

class NativeTorrents {
    static int metadataCalls;

    static String metadata(long session) {
        metadataCalls++;
        return DownloadLibraryCheck.METADATA_V2;
    }
}
