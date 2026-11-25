#pragma once
// The bounds the reader adds to innoextract against untrusted installers (patches/innoextract-1.9-bounds.patch):
// the patched sources include this header and report a bound they hit as a Failure with its status.
#include "failure.hpp"
#include <cstddef>

namespace storm_archive
{
// Charges bytes of installer metadata about to be parsed (inno_backend.cpp): Failure(STORM_ARCHIVE_LIMIT) past the
// budget of one installer.
void inno_charge(std::size_t bytes);
} // namespace storm_archive
