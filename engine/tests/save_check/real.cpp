// storm::save::checkFile on a real save of the game ($STORM_BOOT_SAVE, kept outside the repository): the save passes
// and is not touched; a truncated copy and a copy with a damaged header are refused. Exit 77 without a save.
#include "save_file.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fs = std::filesystem;
using namespace storm::save;

static int failed(const char *what, const std::string &error)
{
    std::fprintf(stderr, "%s: %s\n", what, error.c_str());
    return 1;
}

static fs::path copy(const Bytes &image, const char *name)
{
    const char *tmp = std::getenv("TMPDIR");
    const auto path = fs::path(tmp ? tmp : "/tmp") / name;
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(image.data(), std::streamsize(image.size()));
    return path;
}

int main()
{
    const char *save = std::getenv("STORM_BOOT_SAVE");
    if (!save || !fs::is_regular_file(save))
    {
        std::puts("NOT_RUN: set STORM_BOOT_SAVE to a real save of the game");
        return 77;
    }
    Bytes before, after;
    std::string error;
    if (!readFile(save, before, error))
        return failed("read", error);
    const auto written = fs::last_write_time(save);
    if (!checkFile(save, error))
        return failed("the real save is refused", error);
    if (!readFile(save, after, error) || after != before || fs::last_write_time(save) != written)
        return failed("the check changed the save", error);

    if (checkFile(copy(Bytes(before.begin(), before.begin() + before.size() / 2), "truncated"), error) ||
        !std::strstr(error.c_str(), "Size"))
        return failed("a truncated copy", error.empty() ? "accepted" : error);
    std::printf("truncated: %s\n", error.c_str());
    auto damaged = before;
    const uint32_t outside = static_cast<uint32_t>(damaged.size());
    std::memcpy(damaged.data() + offsetof(Header, dwExtDataOffset), &outside, sizeof(outside));
    error.clear();
    if (checkFile(copy(damaged, "damaged-header"), error) || !std::strstr(error.c_str(), "extOffset"))
        return failed("a copy with a damaged header", error.empty() ? "accepted" : error);
    std::printf("damaged header: %s\n", error.c_str());
    return 0;
}
