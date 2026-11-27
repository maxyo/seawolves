#include "common_strings.hpp"

#include <catch2/catch.hpp>

#include <string>

using storm::strings::CommonStrings;

TEST_CASE("common_strings.numbers_and_names")
{
    CommonStrings strings;
    strings.Append("Yes", "yes");
    strings.Append("", ""); // a line the file could not describe
    strings.Append("No", "no");
    strings.Append("YES", "second"); // a repeated name: the first one is found
    CHECK(strings.Size() == 4);
    CHECK(strings.Find("yes") == 0);
    CHECK(strings.Find("nO") == 2);
    CHECK(strings.Find("maybe") == -1);
    CHECK(strings.Text(1) == nullptr);
    CHECK(strings.Name(4) == nullptr);
    CHECK(std::string(strings.Name(2)) == "No");
}

TEST_CASE("common_strings.merge")
{
    CommonStrings strings;
    strings.Append("Yes", "yes");
    strings.Append("No", "no");
    strings.Merge("NO", "nope"); // a name the table has: its text replaced, its number kept
    strings.Merge("Maybe", "maybe");
    CHECK(strings.Size() == 3);
    CHECK(std::string(strings.Text(1)) == "nope");
    CHECK(strings.Find("maybe") == 2);
    CHECK(std::string(strings.Text(strings.Find("MAYBE"))) == "maybe");

    strings.Clear();
    CHECK(strings.Size() == 0);
    CHECK(strings.Find("yes") == -1);
}
