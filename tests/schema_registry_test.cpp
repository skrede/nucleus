#include "nucleus/schema/schema.h"
#include "nucleus/schema/anchor.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/schema/schema_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using nucleus::anchor;
using nucleus::key_path;
using nucleus::schema_element;
using nucleus::schema_registry;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

bool surface_has(const schema_registry &reg, const char *text)
{
    auto s = reg.surface();
    return std::any_of(s.begin(), s.end(),
                       [&](const key_path &p) { return p.str() == text; });
}

}

TEST_CASE("a root anchor introduces a top-level keyspace", "[schema]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("plexus", anchor::root())));
    REQUIRE(reg.recognizes(path_of("plexus")));
}

TEST_CASE("an element attaches under an already-defined keyspace", "[schema]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("plexus", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("udp", anchor::keyspace(path_of("plexus")))));
    REQUIRE(reg.attach(
        nucleus::element("auth_mode", anchor::keyspace(path_of("plexus/udp")))));

    REQUIRE(reg.recognizes(path_of("plexus/udp/auth_mode")));
    REQUIRE(surface_has(reg, "plexus/udp/auth_mode"));
}

TEST_CASE("referential integrity rejects attach under an undefined keyspace",
          "[schema]")
{
    schema_registry reg;
    auto bad = reg.attach(
        nucleus::element("auth_mode", anchor::keyspace(path_of("plexus/udp"))));
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().find("undefined keyspace") != std::string::npos);
    REQUIRE_FALSE(reg.recognizes(path_of("plexus/udp/auth_mode")));
}

TEST_CASE("required and identity are distinct, independent constraints",
          "[schema]")
{
    // A required field that is not an identity.
    schema_element req = nucleus::required_element("level", anchor::root());
    REQUIRE(req.required);
    REQUIRE_FALSE(req.identity);

    // An identity field that is not required.
    schema_element id = nucleus::identity_element("name", anchor::root());
    REQUIRE(id.identity);
    REQUIRE_FALSE(id.required);

    // Both axes can be set together without one implying the other.
    schema_element both = nucleus::identity_element("id", anchor::root());
    both.required = true;
    REQUIRE(both.required);
    REQUIRE(both.identity);
}

TEST_CASE("the schema is the single surface for CLI and document", "[schema]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("logging", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("level", anchor::keyspace(path_of("logging")))));

    // The same declared set is the document target test and the CLI surface.
    REQUIRE(reg.recognizes(path_of("logging/level")));
    REQUIRE(surface_has(reg, "logging/level"));
    REQUIRE_FALSE(reg.recognizes(path_of("logging/unknown")));
}
