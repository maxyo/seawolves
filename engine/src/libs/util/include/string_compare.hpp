#pragma once

#include "istring.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace storm
{
namespace detail
{
struct is_iequal
{
    template <typename T1, typename T2 = T1> bool operator()(const T1 &first, const T2 &second) const
    {
        return FoldCase(first) == FoldCase(second);
    }
};

} // namespace detail

template <typename Range1T, typename Range2T = Range1T>
bool iEquals(const Range1T &first, const Range2T &second, const size_t count)
{
    detail::is_iequal comp;

    const auto &first_normalized = std::is_pointer<Range1T>::value ? std::string_view(first) : first;
    const auto &second_normalized = std::is_pointer<Range1T>::value ? std::string_view(second) : second;

    const auto first_begin = std::begin(first_normalized);
    const auto second_begin = std::begin(second_normalized);

    const auto first_end = std::end(first_normalized);
    const auto second_end = std::end(second_normalized);

    const auto first_length = std::distance(first_begin, first_end);
    const auto second_length = std::distance(second_begin, second_end);
    if (first_length < count || second_length < count)
    {
        if (first_length != second_length)
        {
            return false;
        }
        else
        {
            return std::equal(first_begin, first_begin + first_length, second_begin, second_begin + second_length,
                              comp);
        }
    }
    else
    {
        return std::equal(first_begin, first_begin + count, second_begin, second_begin + count, comp);
    }
}

template <typename Range1T, typename Range2T = Range1T> bool iEquals(const Range1T &first, const Range2T &second)
{
    const auto &first_normalized = std::is_pointer<Range1T>::value ? std::string_view(first) : first;
    const auto &second_normalized = std::is_pointer<Range1T>::value ? std::string_view(second) : second;

    return traits_cast<ichar_traits<char>>(first_normalized) == traits_cast<ichar_traits<char>>(second_normalized);
}

template <typename Range1T, typename Range2T = Range1T> bool iLess(const Range1T &first, const Range2T &second)
{
    const auto &first_normalized = std::is_pointer<Range1T>::value ? std::string_view(first) : first;
    const auto &second_normalized = std::is_pointer<Range1T>::value ? std::string_view(second) : second;

    return traits_cast<ichar_traits<char>>(first_normalized) < traits_cast<ichar_traits<char>>(second_normalized);
}

template <typename Range1T, typename Range2T = Range1T> bool iLessOrEqual(const Range1T &first, const Range2T &second)
{
    const auto &first_normalized = std::is_pointer<Range1T>::value ? std::string_view(first) : first;
    const auto &second_normalized = std::is_pointer<Range1T>::value ? std::string_view(second) : second;

    return traits_cast<ichar_traits<char>>(first_normalized) <= traits_cast<ichar_traits<char>>(second_normalized);
}

template <typename Range1T, typename Range2T = Range1T> bool iGreater(const Range1T &first, const Range2T &second)
{
    return !iLessOrEqual(first, second);
}

template <typename Range1T, typename Range2T = Range1T> bool iGreaterOrEqual(const Range1T &first, const Range2T &second)
{
    return !iLess(first, second);
}


// Whether `text` matches the file mask `pattern`: '*' stands for any run of bytes (none too), '?' for exactly one
// byte, any other byte for itself, ASCII letters in either case (FoldCase). It works on bytes, not characters: in
// UTF-8 text a '?' matches one byte of a multi-byte character. The masks are the engine's and the scripts' ("*.sav",
// "*.*", "*"; "*.*" needs a dot).
inline bool wildicmp(std::string_view pattern, std::string_view text)
{
    constexpr size_t none = std::string_view::npos;
    size_t p = 0, t = 0;
    // After a mismatch the last '*' takes one byte more of the text and matching resumes behind it.
    size_t star = none, star_text = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            star_text = t;
        }
        else if (p < pattern.size() && (pattern[p] == '?' || FoldCase(pattern[p]) == FoldCase(text[t])))
        {
            ++p;
            ++t;
        }
        else if (star != none)
        {
            p = star + 1;
            t = ++star_text;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

// A UTF-8 file name (std::filesystem::path::u8string), byte for byte.
inline bool wildicmp(std::string_view pattern, std::u8string_view text)
{
    return wildicmp(pattern, std::string_view(reinterpret_cast<const char *>(text.data()), text.size()));
}

class iStrHasher
{
  public:
    size_t operator()(const std::string &key) const
    {
        std::string folded = key;
        std::ranges::transform(folded, folded.begin(), [](char c) { return static_cast<char>(FoldCase(c)); });
        return inner_hasher_(folded);
    }

  private:
    std::hash<std::string> inner_hasher_;
};

struct iStrComparator
{
    bool operator()(const std::string &left, const std::string &right) const
    {
        return iEquals(left, right);
    }
};
} // namespace storm
