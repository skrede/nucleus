#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/schema/cli_flag.h"

#include "nucleus/argv/argv_source.h"

#include "nucleus/utility/escaped_text.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>
#include <string_view>

using nucleus::anchor;
using nucleus::keyspace;
using nucleus::key_path;
using nucleus::argv_source;
using nucleus::cli_delimiter;
using nucleus::schema_registry;
using nucleus::schema_enforcer;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

schema_registry build_schema()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("plexus", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("udp", anchor::keyspace(path_of("plexus")))));
    REQUIRE(reg.attach(nucleus::required_element(
        "auth_mode", anchor::keyspace(path_of("plexus/udp")))));
    return reg;
}

std::string refusal_of(std::string_view text)
{
    const auto parsed = cli_delimiter::parse(text);
    REQUIRE_FALSE(parsed);
    return parsed.error();
}

bool mentions(const std::string &reason, std::string_view part) { return reason.find(part) != std::string::npos; }
}

// One model: the SAME schema drives both the CLI surface a flag is validated
// against and the document/keyspace structure the resolved value is validated
// against. A schema change moves both at once, so flag set and document shape
// cannot drift.
TEST_CASE("the schema is the single authority over CLI and document", "[bijection]")
{
    schema_registry reg = build_schema();

    // The flag set is the schema surface projected through the bijection.
    auto surface = reg.surface();
    bool has_flag = std::any_of(surface.begin(), surface.end(),
        [](const key_path &p) { return nucleus::flag_of(p) == "--plexus-udp-auth_mode"; });
    REQUIRE(has_flag);

    // A flag is validated against that same schema surface after mapping.
    argv_source src(std::vector<std::string>{"--plexus-udp-auth_mode=auth"});
    src.recognize_with([&](const key_path &p) { return reg.recognizes(p); });

    auto batch = src.pull();
    REQUIRE(batch);

    // The mapped entries feed the keyspace, and the resolved keyspace validates
    // against the very same schema -- one authority, two projections.
    keyspace ks;
    for(const auto &e : batch.value().entries)
        ks.set(path_of(e.path.c_str()), e.value.to_owned());

    REQUIRE(schema_enforcer::validate(reg, ks));
}

TEST_CASE("an undeclared flag is rejected by the schema authority", "[bijection]")
{
    schema_registry reg = build_schema();
    argv_source src(std::vector<std::string>{"--plexus-udp-unknown=x"});
    src.recognize_with([&](const key_path &p) { return reg.recognizes(p); });

    auto batch = src.pull();
    REQUIRE_FALSE(batch); // strict: the schema does not declare this path
}

// A digit or bracket delimiter collides with ordinal-index notation, so flag
// segmentation would be ambiguous. Both belong to the same non-invertible class
// as `=` and `/` and are rejected at parse time.
TEST_CASE("a digit delimiter is rejected", "[bijection]")
{
    REQUIRE(mentions(refusal_of("0"), "all digits"));
    REQUIRE(mentions(refusal_of("42"), "all digits"));
}

// Under `0x` a partly typed `--cluster0xnode0x30` is both ordinal 30 and ordinal 3 followed by
// the delimiter's leading `0`, so only a digit as the delimiter's first byte is ambiguous.
TEST_CASE("a digit-led delimiter is rejected and a trailing digit is not", "[bijection]")
{
    for(const char *text : {"0x", "1-", "9_"})
    {
        const std::string reason = refusal_of(text);
        REQUIRE(mentions(reason, "ordinal-index notation"));
        REQUIRE(mentions(reason, std::string("'") + text + "'"));
    }
    for(const char *text : {"x0", "a1b", "-", "__", ".", "/", "*"})
        REQUIRE(cli_delimiter::parse(text));
}

TEST_CASE("a refused delimiter is quoted escaped in its reason", "[bijection]")
{
    const std::string reason = refusal_of("a\nb");
    REQUIRE(mentions(reason, "'a\\nb'"));
    REQUIRE_FALSE(mentions(reason, "\n"));
}

TEST_CASE("a delimiter carrying a display control is rejected", "[bijection]")
{
    for(const std::string_view text : {"\t", "\x1b", "a\302\233b", "\xe2\x80\xae", "a\177b"})
    {
        const std::string reason = refusal_of(text);
        REQUIRE(mentions(reason, "must not contain a control character"));
        REQUIRE(mentions(reason, "'" + nucleus::escaped_text(text) + "'"));
    }
    REQUIRE(mentions(refusal_of("\n"), "newline or a carriage return"));
    REQUIRE(mentions(refusal_of("\r"), "newline or a carriage return"));
    for(const char *text : {"$(x)", "::", "gr\303\266\303\237e"})
        REQUIRE(cli_delimiter::parse(text));
}

TEST_CASE("a bracket-containing delimiter is rejected", "[bijection]")
{
    REQUIRE_FALSE(cli_delimiter::parse("["));
    REQUIRE_FALSE(cli_delimiter::parse("]"));
    REQUIRE_FALSE(cli_delimiter::parse("_[_"));
}

TEST_CASE("a conventional delimiter still parses", "[bijection]")
{
    REQUIRE(cli_delimiter::parse("-"));
    REQUIRE(cli_delimiter::parse("__"));
    REQUIRE(cli_delimiter::parse("."));
}
