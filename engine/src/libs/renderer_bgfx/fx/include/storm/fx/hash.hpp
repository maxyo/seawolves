// A 64-bit content hash for the bakes' cache keys: XXH3 (xxHash) over every piece, each preceded by its size (so
// pieces never run into each other). Not for anything an attacker chooses.
#pragma once

#include <xxhash.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace storm::fx
{

class Hasher
{
  public:
    Hasher()
    {
        XXH3_64bits_reset(&state_);
    }
    Hasher &Bytes(const void *data, size_t size)
    {
        const uint64_t length = size;
        XXH3_64bits_update(&state_, &length, sizeof(length));
        XXH3_64bits_update(&state_, data, size);
        return *this;
    }
    template <class T> Hasher &Value(const T &value)
    {
        static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>, "a struct's padding would be hashed");
        return Bytes(&value, sizeof(value));
    }
    // Arrays of numbers, or of std::array or GLM vectors of numbers (positions, texture coordinates): no padding in
    // them.
    template <class T> Hasher &Span(std::span<const T> values)
    {
        return Bytes(values.data(), values.size_bytes());
    }
    uint64_t Result() const
    {
        return XXH3_64bits_digest(&state_);
    }

  private:
    XXH3_state_t state_;
};

} // namespace storm::fx
