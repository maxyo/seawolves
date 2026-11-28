#include "text_utf8.h"

namespace storm::ui
{

namespace
{

bool Continuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

} // namespace

std::string Lower(std::string_view text)
{
    return Rml::StringUtilities::ToLower(std::string(text));
}

std::string Upper(std::string_view text)
{
    return Rml::StringUtilities::ToUpper(std::string(text));
}

std::string Capitalized(std::string_view text)
{
    size_t first = text.empty() ? 0 : 1;
    while (first < text.size() && Continuation(text[first]))
        ++first;
    return Upper(text.substr(0, first)) + std::string(text.substr(first));
}

std::string_view Utf8Prefix(std::string_view text, size_t bytes)
{
    if (bytes >= text.size())
        return text;
    while (bytes > 0 && Continuation(text[bytes]))
        --bytes;
    return text.substr(0, bytes);
}

} // namespace storm::ui
