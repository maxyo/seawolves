// An attribute that holds a script reference (ATTRIBUTES::SetReference): every way of giving it text makes it plain,
// and moving the attribute keeps the reference. The save writes what is left; a reference that survives a text write
// would be saved instead of the text.
#include "attributes.h"
#include "data.h"

#include "core_fake.hpp"

#include <catch2/catch.hpp>

TEST_CASE("text written into an attribute replaces its script reference")
{
    storm::test::Codec codec;
    DATA target{VAR_OBJECT};
    ATTRIBUTES root(codec);

    root.SetReference(&target);
    root.SetValue(std::string_view("plain"));
    CHECK(root.GetReference() == nullptr);

    root.SetReference(&target);
    root.SetValue("plain");
    CHECK(root.GetReference() == nullptr);

    ATTRIBUTES &child = root.CreateAttribute("actor");
    const uint32_t code = child.GetThisNameCode();
    child.SetReference(&target);
    root.SetAttribute(code, "plain");
    CHECK(child.GetReference() == nullptr);
    child.SetReference(&target);
    root.SetAttribute(code, std::string_view("plain"));
    CHECK(child.GetReference() == nullptr);
    child.SetReference(&target);
    root.SetAttribute("actor", std::string_view("plain"));
    CHECK(child.GetReference() == nullptr);
}

TEST_CASE("a script reference replaces the text, and a moved attribute keeps it")
{
    storm::test::Codec codec;
    DATA target{VAR_OBJECT};
    ATTRIBUTES attribute(codec);
    attribute.SetValue("text");
    attribute.SetReference(&target);
    CHECK(!attribute.HasValue());
    ATTRIBUTES moved(std::move(attribute));
    CHECK(moved.GetReference() == &target);
}
