// Prints the build ID moduleBuildId (src/libs/util/include/platform/module_build_id.hpp) reads for this program and
// for a function of libc, which run.py compares with what readelf reads from the files.
#include "platform/module_build_id.hpp"

#include <cstdio>
#include <cstdlib>

#ifndef VARIANT
#define VARIANT 0
#endif

int variant()
{
    return VARIANT;
}

int main()
{
    std::printf("%s %d\n", storm::platform::moduleBuildId(reinterpret_cast<const void *>(&variant)).c_str(), variant());
    std::printf("%s\n", storm::platform::moduleBuildId(reinterpret_cast<const void *>(&std::abort)).c_str());
    try
    {
        static const int not_code = 0;
        storm::platform::moduleBuildId(
            reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(&not_code) + (uintptr_t(1) << 46)));
        std::puts("an address outside every module was given a build ID");
        return 1;
    }
    catch (const std::runtime_error &)
    {
    }
    return 0;
}
