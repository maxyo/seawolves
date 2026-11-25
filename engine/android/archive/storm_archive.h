#pragma once
#include <stdint.h>
#include <stddef.h>
#include "storm_archive_status.h" /* generated: cmake/StormAppContract.cmake */
#ifdef __cplusplus
extern "C"
{
#endif
    /* Discovery of game roots and nested containers, and extraction of one of them. Nothing found in a source is
     * ever run. A source is a private directory holding the player's files (copied picker
     * documents or a downloaded set), or one file in such a directory (its Inno
     * volumes or archive volumes are then taken from that directory and nothing
     * else is read). Discovery finds every complete game root
     * (engine.ini + PROGRAM/ + RESOURCE/) in loose folders, archives, Inno
     * installers with their volumes, multi-volume archives and containers nested
     * up to max_depth levels. Nested containers are written to scratch_dir, which
     * must be an existing empty private directory outside the source; the caller
     * keeps it until extraction finished and then deletes it. Staging (extraction) must be an existing empty private
     * directory, separate from the source and from live data; on failure the caller discards it whole. */
    enum storm_archive_stage
    {
        STORM_ARCHIVE_STAGE_READING = 1, /* listing sources and containers */
        STORM_ARCHIVE_STAGE_NESTED = 2,  /* writing nested containers to scratch */
        STORM_ARCHIVE_STAGE_INSTALL = 3  /* writing the selected game into staging */
    };
    /* bytes/total are for the current stage (total 0 when unknown); files counts
     * created game files. Return nonzero to cancel. Called on the calling thread. */
    typedef int (*storm_archive_stage_progress)(void *, int stage, const char *path, uint64_t bytes, uint64_t total,
                                                uint64_t files);
    /* Bounds of one call on untrusted input, all required and positive except reserve_bytes; the values the app
     * passes are in README. */
    struct storm_archive_source_limits
    {
        uint64_t max_total_bytes;   /* Decoded bytes. */
        uint64_t max_file_bytes;    /* One game file. */
        uint64_t max_entries;       /* Entries seen. */
        uint64_t max_seconds;       /* Duration. */
        uint64_t max_scratch_bytes; /* Nested containers in scratch. */
        uint64_t max_depth;         /* Nested container levels, 1..3. */
        uint64_t max_ratio;         /* Decoded:compressed bound per container. */
        uint64_t reserve_bytes;     /* Free space kept on the device. */
    };
    /* *json (free with storm_archive_free) is the discovery document on success.
     * On ENCRYPTED, NO_SPACE or LIMIT it describes the failure; see README.
     * password may be NULL; it is applied to every container that needs one.
     * patch_layer is the engine's script patch layer directory (Android:
     * <cache root>/layers/script-patches/current): every copy whose identify files can be
     * read is identified with the engine's own rule (script_patches::identify).
     * NULL means the caller does not identify copies (every candidate is then
     * "identified":false and "patch_sets" is null); a path that is not a
     * directory is an IO failure and a damaged layer INVALID, never "no set". */
    int storm_archive_discover(const char *source, const char *scratch_dir, const char *password,
                               const char *patch_layer, const struct storm_archive_source_limits *limits,
                               storm_archive_stage_progress progress, void *userdata, char **json, char *error,
                               size_t error_capacity);
    /* {"patch_sets":[{"id","game","version","identify":[<resource key>]}]} of a
     * script patch layer directory: the files a caller copies for discovery. */
    int storm_archive_patch_layer(const char *patch_layer, char **json, char *error, size_t error_capacity);
    /* Extracts exactly one discovered root (its opaque id) into empty_staging.
     * Source and scratch must be unchanged since discovery. */
    int storm_archive_extract_root(const char *source, const char *scratch_dir, const char *password,
                                   const char *root_id, const char *empty_staging,
                                   const struct storm_archive_source_limits *limits,
                                   storm_archive_stage_progress progress, void *userdata, char **json, char *error,
                                   size_t error_capacity);
    void storm_archive_free(char *json);
    /* Removes the file or directory tree at the absolute `path` (nothing to do when it is absent) by the bytes of
     * each name, so a tree with names that are not UTF-8 can be removed; links are removed, never followed. */
    int storm_archive_remove_tree(const char *path, char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
