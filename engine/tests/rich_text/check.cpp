#include "rich_text.hpp"

#include <catch2/catch.hpp>

using namespace storm::xi;

namespace
{
std::vector<RichItem> Parse(std::string_view text, std::vector<std::string> *dropped = nullptr)
{
    return ParseRichText(text, [&](std::string_view tag) {
        if (dropped)
            dropped->emplace_back(tag);
    });
}

const RichText &TextAt(const std::vector<RichItem> &items, size_t i)
{
    return std::get<RichText>(items.at(i));
}
} // namespace

TEST_CASE("rich_text.styles")
{
    const auto items = Parse("a[color=255,10,20,30]b[scale=1.5][font=BOLD]c[/font][/scale]d[/color]e");
    REQUIRE(items.size() == 5);
    CHECK(TextAt(items, 0).text == "a");
    CHECK_FALSE(TextAt(items, 0).style.color_set);
    CHECK(TextAt(items, 1).style.color == 0xFF0A141E);
    CHECK(TextAt(items, 2).style.font == "BOLD");
    CHECK(TextAt(items, 2).style.scale == 1.5f);
    CHECK(TextAt(items, 2).style.color == 0xFF0A141E); // styles nest
    CHECK(TextAt(items, 3).style.font.empty());
    CHECK(TextAt(items, 3).style.scale == 1.0f);
    CHECK(TextAt(items, 3).style.color_set);
    CHECK_FALSE(TextAt(items, 4).style.color_set);

    const auto spaced = Parse("[color=128, 1, 2, 3]x[voffset=-4]y[/voffset][voffset=6]z");
    CHECK(TextAt(spaced, 0).style.color == 0x80010203);
    CHECK(TextAt(spaced, 1).style.voffset == 0); // a negative offset is none
    CHECK(TextAt(spaced, 2).style.voffset == 6);
}

TEST_CASE("rich_text.icons_breaks_and_plain_brackets")
{
    std::vector<std::string> dropped;
    const auto items =
        Parse("A[icon:group=G\\_X,pic=P,width=20,height=10,voffset=3,layer=back][icon:group=N,pic=Q,width="
              "8,height=8,color=255,1,2,3]\\nB\r\n   C\n\rD[b]E[Pay 4 pesos]F [x",
              &dropped);
    REQUIRE(items.size() == 11);
    const auto &back = std::get<RichIcon>(items[1]);
    CHECK(back.group == "G\\_X");
    CHECK(back.picture == "P");
    CHECK((back.width == 20 && back.height == 10 && back.voffset == 3 && back.back));
    const auto &inline_icon = std::get<RichIcon>(items[2]);
    CHECK_FALSE(inline_icon.back);
    CHECK(inline_icon.color == 0xFF010203);
    CHECK(std::holds_alternative<RichLineBreak>(items[3]));
    CHECK(TextAt(items, 4).text == "B");
    CHECK(std::holds_alternative<RichLineBreak>(items[5])); // CR LF is one break, the indent after it is dropped
    CHECK(TextAt(items, 6).text == "C");
    CHECK(std::holds_alternative<RichLineBreak>(items[7])); // so is LF CR
    CHECK(TextAt(items, 8).text == "D");
    CHECK(TextAt(items, 9).text == "E");
    CHECK(TextAt(items, 10).text == "F [x"); // a `[` that opens no tag is text
    CHECK(dropped == std::vector<std::string>{"[b]", "[Pay 4 pesos]"});
    CHECK(TextAt(Parse("a[]b[=c]"), 0).text == "a[]b[=c]"); // no tag without a name
}

TEST_CASE("rich_text.width")
{
    // one pixel per byte, times the scale
    const auto width = [](std::string_view text, const RichStyle &style) {
        return static_cast<int32_t>(text.size() * style.scale);
    };
    CHECK(RichTextWidth(Parse("abcd"), width) == 4);
    CHECK(RichTextWidth(Parse("ab[scale=2]cd[/scale]\nabcdefg"), width) == 7);
    CHECK(RichTextWidth(Parse("ab[scale=2]cd[/scale]\nabcde"), width) == 6);
    CHECK(RichTextWidth(Parse("a[icon:group=G,pic=P,width=10,height=4]b"), width) == 12);
    CHECK(RichTextWidth(Parse("a[icon:group=G,pic=P,width=10,height=4,layer=back]b"), width) == 2);
}
