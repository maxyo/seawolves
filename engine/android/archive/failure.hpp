#pragma once
#include "storm_archive.h"
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace storm_archive
{
// What went wrong, as the status the C API returns (storm_archive_status). missing names the volumes a
// STORM_ARCHIVE_MISSING_VOLUME failure lacks; they go back to the caller in the result document.
struct Failure : std::runtime_error
{
    int status;
    std::vector<std::string> missing;
    Failure(int code, const std::string &message, std::vector<std::string> missing_volumes = {})
        : std::runtime_error(message), status(code), missing(std::move(missing_volumes))
    {
    }
};
} // namespace storm_archive
