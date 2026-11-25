#include "storm/fx/parallax_exclusions.hpp"

#include "storm/fx/height_map.hpp"
#include "storm/fx/parallax_trust.hpp"

#include <cctype>
#include <sstream>

namespace storm::fx
{
namespace
{
std::string Trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return std::string(s);
}

std::string WithoutComment(const std::string &line)
{
    return Trim(std::string_view(line).substr(0, line.find('#')));
}

} // namespace

std::string_view ParallaxExclusions::ShippedText()
{
    static constexpr std::string_view text =
#include "parallax_exclusions.inc"
        ;
    return text;
}

namespace
{
// `include <name>` / `exclude <name>` of a line without its comment; false when it is neither.
bool ParseLine(const std::string &content, std::string &name, bool &include)
{
    std::istringstream words(content);
    std::string verb, extra;
    words >> verb >> name;
    include = verb == "include";
    return !(words >> extra) && (verb == "exclude" || verb == "include") && !name.empty();
}
} // namespace

void ParallaxExclusions::LoadShipped(std::string_view text, std::vector<std::string> *problems)
{
    shipped_.clear();
    std::istringstream in{std::string(text)};
    std::string line;
    int number = 0;
    while (std::getline(in, line))
    {
        ++number;
        const std::string content = WithoutComment(line);
        if (content.empty())
            continue;
        std::string name;
        bool include = false;
        if (ParseLine(content, name, include))
            shipped_[name] = include;
        else if (problems)
            problems->push_back("shipped list line " + std::to_string(number) + ": '" + content +
                                "' is not `include <texture>` or `exclude <texture>`");
    }
}

ParallaxExclusions::Verdict ParallaxExclusions::Of(std::string_view normalized_name) const
{
    const std::string name(normalized_name);
    if (const auto it = shipped_.find(name); it != shipped_.end())
        return it->second ? Verdict::Included : Verdict::Excluded;
    return Verdict::Default;
}

bool ParallaxExclusions::Allows(std::string_view normalized_name, ReliefClass relief, float trust_coverage) const
{
    switch (Of(normalized_name))
    {
    case Verdict::Excluded:
        return false;
    case Verdict::Included:
        return true;
    case Verdict::Default:
        break;
    }
    return ReliefTakesParallax(relief) && trust_coverage >= kParallaxMinCoverage;
}

} // namespace storm::fx
