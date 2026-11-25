#pragma once
// The GNU build ID of a loaded ELF module (Linux, Android): the linker's hash of the linked file (NT_GNU_BUILD_ID,
// -Wl,--build-id). It changes whenever the module's code or data change and only then: a rebuilt identical binary,
// a ccache hit or a new commit that does not touch the module keep it.
#include "../hex.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <stdexcept>
#include <string>

namespace storm::platform
{
namespace detail
{
struct BuildIdSearch
{
    uintptr_t address = 0;
    bool found_module = false;
    std::string id;
};

inline size_t noteAlign(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

inline int findBuildId(dl_phdr_info *info, size_t, void *data)
{
    auto &search = *static_cast<BuildIdSearch *>(data);
    bool inside = false;
    for (int i = 0; i < info->dlpi_phnum && !inside; ++i)
    {
        const auto &segment = info->dlpi_phdr[i];
        const uintptr_t start = info->dlpi_addr + segment.p_vaddr;
        inside = segment.p_type == PT_LOAD && search.address >= start && search.address < start + segment.p_memsz;
    }
    if (!inside)
        return 0;
    search.found_module = true;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const auto &segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_NOTE)
            continue;
        const size_t alignment = segment.p_align >= 8 ? 8 : 4;
        const auto *notes = reinterpret_cast<const unsigned char *>(info->dlpi_addr + segment.p_vaddr);
        for (size_t at = 0; at + sizeof(ElfW(Nhdr)) <= segment.p_memsz;)
        {
            ElfW(Nhdr) note;
            std::memcpy(&note, notes + at, sizeof note);
            const size_t name = at + sizeof note;
            const size_t desc = name + noteAlign(note.n_namesz, alignment);
            const size_t next = desc + noteAlign(note.n_descsz, alignment);
            if (next > segment.p_memsz)
                break;
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 && std::memcmp(notes + name, "GNU", 4) == 0 &&
                note.n_descsz > 0)
            {
                search.id = ToHex({reinterpret_cast<const char *>(notes + desc), note.n_descsz});
                return 1;
            }
            at = next;
        }
    }
    return 1;
}
} // namespace detail

// The build ID, in hex, of the loaded module that contains `address` (a function of that module). Throws
// std::runtime_error when no module contains it or the module was linked without a build ID.
inline std::string moduleBuildId(const void *address)
{
    detail::BuildIdSearch search;
    search.address = reinterpret_cast<uintptr_t>(address);
    dl_iterate_phdr(detail::findBuildId, &search);
    if (!search.found_module)
        throw std::runtime_error("no loaded module contains the address");
    if (search.id.empty())
        throw std::runtime_error("the module has no GNU build ID (link it with -Wl,--build-id)");
    return search.id;
}
} // namespace storm::platform
