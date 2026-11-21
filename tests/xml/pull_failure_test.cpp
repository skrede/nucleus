#include "nucleus/error.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

nucleus::xml_source xml_of(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

}

TEST_CASE("a pathologically deep document fails with a depth-cap error, not a crash",
          "[xml][robustness]")
{
    constexpr int levels = 100;
    std::string doc;
    for(int i = 0; i < levels; ++i)
        doc += "<a>";
    doc += "v";
    for(int i = 0; i < levels; ++i)
        doc += "</a>";

    auto src = xml_of(doc);
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("depth cap") != std::string::npos);
}

TEST_CASE("a nonexistent file reports an unreadable file, not a parse failure",
          "[xml][robustness]")
{
    auto src = nucleus::xml_source::from(
        nucleus::xml_source_options::of_file("no/such/directory/config.xml"));
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("cannot read file") != std::string::npos);
    REQUIRE(pulled.error().message.find("no/such/directory/config.xml") != std::string::npos);
    REQUIRE(pulled.error().message.find("parse") == std::string::npos);
}

TEST_CASE("garbage input reports a parse failure with an offset", "[xml][robustness]")
{
    auto src = xml_of("\x01\x02 this is not xml at all >>>");
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("failed to parse input") != std::string::npos);
    REQUIRE(pulled.error().message.find("offset") != std::string::npos);
}

TEST_CASE("an element-free document reports the missing document element",
          "[xml][robustness]")
{
    // pugixml itself rejects a document with no element at parse time, so this
    // surfaces through the parse-failure branch with pugixml's description.
    auto src = xml_of("<!-- only a comment, no root -->");
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("document element") != std::string::npos);
}
