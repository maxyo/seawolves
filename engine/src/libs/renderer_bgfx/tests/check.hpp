// CHECK for the check programs of the renderer and the relief library: a failed condition is printed and counted.
#pragma once

#include <cstdio>

inline int failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false)
