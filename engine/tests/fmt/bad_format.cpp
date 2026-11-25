// Must not compile: an integer format specifier for a string argument (tests/CMakeLists.txt fmt.rejects_bad_format).
#include <fmt/format.h>
auto bad = fmt::format(FMT_STRING("{:d}"), "text");
