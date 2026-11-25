// The shipped parallax list (data, src/parallax_exclusions.inc). A texture takes the parallax by default when its class
// does (ReliefTakesParallax: masonry and characters) and its trusted share reaches kParallaxMinCoverage; the list
// decides the exceptions either way. One texture a line: `include <name>` or `exclude <name>`, the name as
// NormalizeTexturePath gives it ("locations/bump_city"); '#' starts a comment; the last line for a name wins.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace storm::fx
{

class ParallaxExclusions
{
  public:
    enum class Verdict
    {
        Default,  // the class and the trust coverage decide
        Excluded, // `exclude` in the list
        Included, // `include` in the list
    };

    // The shipped list parsed from its text (ShippedText()); lines that are not a name are reported in `problems`.
    void LoadShipped(std::string_view text, std::vector<std::string> *problems = nullptr);

    Verdict Of(std::string_view normalized_name) const;
    // Whether the texture takes the parallax: the list's verdict, else its class and trusted share.
    bool Allows(std::string_view normalized_name, ReliefClass relief, float trust_coverage) const;
    size_t shipped_count() const
    {
        return shipped_.size();
    }

    static std::string_view ShippedText();

  private:
    std::unordered_map<std::string, bool> shipped_; // true: include
};

} // namespace storm::fx
