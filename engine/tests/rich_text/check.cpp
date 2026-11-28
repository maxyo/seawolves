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
    CHECK_FALSE(TextAt(items, 3).style.scale); // the node's
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
        return static_cast<int32_t>(text.size() * style.scale.value_or(1.0f));
    };
    CHECK(RichTextWidth(Parse("abcd"), width) == 4);
    CHECK(RichTextWidth(Parse("ab[scale=2]cd[/scale]\nabcdefg"), width) == 7);
    CHECK(RichTextWidth(Parse("ab[scale=2]cd[/scale]\nabcde"), width) == 6);
    CHECK(RichTextWidth(Parse("a[icon:group=G,pic=P,width=10,height=4]b"), width) == 12);
    CHECK(RichTextWidth(Parse("a[icon:group=G,pic=P,width=10,height=4,layer=back]b"), width) == 2);
}

TEST_CASE("rich_text.layout")
{
    // one pixel per byte times the scale; glyphs 10 px high times the scale
    const RichMeasure measure{
        [](std::string_view text, const RichStyle &style) { return text.size() * style.scale.value_or(1.0f); },
        [](const RichStyle &style) { return 10.0f * style.scale.value_or(1.0f); }};
    const auto lines = LayoutRichText(Parse("one two three four"), 9.0f, measure, 12.0f, false);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].pieces.at(0).text == "one two");
    CHECK(lines[1].pieces.at(0).text == "three");
    CHECK(lines[2].pieces.at(0).text == "four");
    CHECK(lines[1].height == 12.0f);

    const auto wide = LayoutRichText(Parse("abcdefghijkl x"), 5.0f, measure, 12.0f, false);
    REQUIRE(wide.size() == 2); // a word longer than the line keeps a line of its own
    CHECK(wide[0].pieces.at(0).text == "abcdefghijkl");

    // styles and icons keep their x; a break starts a line
    const auto mixed = LayoutRichText(Parse("ab[scale=2]cd[/scale][icon:group=G,pic=P,width=5,height=30]\ne"), 100.0f,
                                      measure, 12.0f, true);
    REQUIRE(mixed.size() == 2);
    CHECK(mixed[0].pieces.size() == 3);
    CHECK(mixed[0].pieces[1].x == 2.0f);
    CHECK(mixed[0].pieces[2].x == 6.0f);
    CHECK(mixed[0].width == 11.0f);
    CHECK(mixed[0].height == 30.0f); // adaptive: the icon's height
    CHECK(mixed[1].height == 12.0f);

    const auto big = LayoutRichText(Parse("[scale=2]x[/scale]"), 100.0f, measure, 12.0f, true);
    CHECK(big[0].height == 22.0f); // larger glyphs: line space plus the difference
    CHECK(LayoutRichText(Parse("[scale=2]x[/scale]"), 100.0f, measure, 12.0f, false)[0].height == 12.0f);
    // an icon behind the text takes no room
    const auto back =
        LayoutRichText(Parse("ab[icon:group=G,pic=P,width=5,height=50,layer=back]"), 100.0f, measure, 12.0f, true);
    CHECK(back[0].height == 12.0f);
    CHECK(back[0].width == 2.0f);
}

TEST_CASE("rich_text.plain")
{
    CHECK(RichPlainText(Parse("[color=255,1,2,3]Head[/color]\\n[scale=0.8]a [font=B]b[/font][/scale]"
                              "[icon:group=G,pic=P,width=8,height=8] c\nd[x]")) == "Head\na b c\nd");
    CHECK(RichPlainText(Parse("")).empty());
}
