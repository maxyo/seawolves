#pragma once
#include "failure.hpp"
#include "storm_archive.h"
#include "volume_names.hpp"
#include "platform/layer_files.hpp"
#include <chrono>
#include <memory>
#include <functional>
#include <string>
#include <set>
#include <cstdint>
#include <vector>
#include <utility>
struct storm_archive_layer
{
    std::shared_ptr<const storm::layers::Layer> layer;
};
namespace storm_archive
{
bool allocation_failed();
void clear_allocation_failure();
// The one exception boundary of the C entry points: runs call and returns its status, the message of a failure in
// error (cut before a whole character). With json, call writes the result document there; a failed call leaves the
// document it filled into `failure` (encrypted, no_space) or, for a Failure naming missing volumes,
// {"missing_volumes":[…]}. Running out of the reader's memory is STORM_ARCHIVE_MEMORY, whatever threw.
int boundary(char **json, char *error, size_t capacity, const std::function<void(std::string &failure)> &call);
// A result document the caller frees with storm_archive_free.
char *result_json(const std::string &text);
std::string json_quote(const std::string &s);
// Lower-case hex of bytes, and back (the opaque game root id); from_hex refuses anything else as an invalid id.
std::string to_hex(const std::string &bytes);
std::string from_hex(const std::string &text);
struct Fd
{
    int value;
    explicit Fd(int fd = -1) : value(fd)
    {
    }
    ~Fd();
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};
// Validated relative member path split into components: UTF-8 (every name the reader writes or reports is), no
// traversal, control characters (C0, DEL, C1), drive/ADS colons, empty components or Windows trailing dots.
std::vector<std::string> path_parts(const std::string &raw);
// A name for messages and logs: bytes that are not UTF-8, and control characters, as \xNN.
std::string printable_name(const std::string &raw);
// Windows-1251 text (old game files) as UTF-8; a byte the code page does not define becomes '?'.
std::string windows1251_text(const std::string &s);
// Writes a warning to the log (logcat on Android, stderr on a host).
void warn(const std::string &message);
// Entry names of one container that are not UTF-8 (formats without a defined name encoding: ZIP without the UTF-8
// flag and its Unicode path field, RAR 4 without Unicode names, tar, CAB, plain ISO 9660). decode() returns UTF-8
// names unchanged and reads any other name as Russian DOS (CP866) or Windows (CP1251) text, whichever gives a
// Cyrillic name (the one with more letters; CP866 when both fit equally); a name that is neither is an error naming
// the entry. report() logs how the container's names were read.
struct LegacyNames
{
    uint64_t as_dos = 0, as_windows = 0, ambiguous = 0;
    std::string example;
    std::string decode(const std::string &raw);
    void report(const std::string &container) const;
};
// ASCII lower case: names compare case-insensitively, as Android storage may.
using volume_names::lower;
std::string join_parts(const std::vector<std::string> &parts, size_t count);
// Game-root anchors: RESOURCE/ or PROGRAM/ directory, engine.ini or options leaf.
enum Anchor
{
    NO_ANCHOR,
    RESOURCE_DIR,
    PROGRAM_DIR,
    ENGINE_INI,
    OPTIONS_FILE
};
Anchor anchor_at(const std::vector<std::string> &parts, size_t index);
// Output path inside the staging root for a member anchored at index, or empty
// for files that are never written (executables, libraries, scripts).
std::string root_output(const std::vector<std::string> &parts, size_t index);
// Free bytes for an unprivileged writer on the filesystem holding fd.
uint64_t free_bytes(int fd);
struct Context
{
    // The staging directory written to (none: a context that only bounds reading).
    Fd root;
    storm_archive_source_limits limits;
    storm_archive_stage_progress progress;
    void *userdata;
    int stage = 0;
    uint64_t stage_total = 0;
    uint64_t container_input = 0, container_decoded = 0;
    uint64_t written_since_space_check = 0;
    // The game root written (its prefix in the container, "" or ending in '/'): members of other roots are skipped.
    std::string selected_root;
    std::string password;
    uint64_t total = 0, entries = 0, extracted = 0;
    bool resources = false, programs = false, engine = false;
    std::set<std::string> paths;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    Context(const char *output, const storm_archive_source_limits &, storm_archive_stage_progress, void *);
    void check(const std::string &path = "");
    void account(uint64_t n, const std::string &path);
    void begin_container(uint64_t input_bytes)
    {
        container_input = input_bytes;
        container_decoded = 0;
    }
    std::string select(const std::string &path);
    int create(const std::string &path);
    void write(int fd, const char *data, size_t size);
    void finish();
};
// All literal {app} user files of an Inno installer (normalized, not root-mapped) with their sizes, its header's
// title and version, and the sibling volume names the payload needs. Metadata only.
struct InnoListing
{
    std::string title, version;
    std::vector<std::pair<std::string, uint64_t>> files;
    std::vector<std::string> volumes;
};
InnoListing inno_listing(const std::string &input);
bool inno_probe(const std::string &input);
void inno_extract(const std::string &input, Context &ctx);
// Every volume of one logical archive in order (one entry for a plain file).
void archive_extract(const std::vector<std::string> &volumes, Context &ctx);
// Sequential walk without writing output. entry() returns true to receive the
// member's data through chunk(); chunk() returns false to skip the rest.
struct ArchiveVisitor
{
    virtual ~ArchiveVisitor() = default;
    virtual bool entry(const std::string &path, uint64_t size) = 0;
    virtual bool chunk(const char *data, size_t size) = 0;
    virtual void done() = 0;
};
std::string archive_walk(const std::vector<std::string> &volumes, Context &ctx, ArchiveVisitor &visitor);
bool container_signature(const char *head, size_t size);
bool inno_volume_signature(const char *head, size_t size);
std::string checked_input(const char *input);
} // namespace storm_archive
