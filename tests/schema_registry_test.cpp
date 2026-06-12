#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

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

TEST_CASE("unique and primary-key are distinct uniqueness axes", "[schema][unique]")
{
    // A unique field carries the value-uniqueness constraint without the selector
    // role of a primary key.
    schema_element u = nucleus::unique_element("serial", anchor::root());
    REQUIRE(u.unique);
    REQUIRE_FALSE(u.identity);
    REQUIRE(u.enforces_uniqueness());

    // A primary key is uniqueness-bearing even without the unique flag set.
    schema_element k = nucleus::primary_key_element("name", anchor::root());
    REQUIRE(k.identity);
    REQUIRE_FALSE(k.unique);
    REQUIRE(k.enforces_uniqueness());

    // primary_key_element and identity_element are the same concept.
    schema_element id = nucleus::identity_element("name", anchor::root());
    REQUIRE(id.identity == k.identity);

    // An ordinary element bears no uniqueness constraint.
    REQUIRE_FALSE(nucleus::element("x", anchor::root()).enforces_uniqueness());
}

TEST_CASE("many unique fields may share one container", "[schema][unique]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("server", anchor::keyspace(path_of("cluster")))));

    // Several unique fields under the same container are all admissible -- unique
    // is a constraint, not a singular role.
    REQUIRE(reg.attach(
        nucleus::unique_element("serial", anchor::keyspace(path_of("cluster/server")))));
    REQUIRE(reg.attach(
        nucleus::unique_element("mac", anchor::keyspace(path_of("cluster/server")))));
}

TEST_CASE("a config space has exactly one primary key", "[schema][unique]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("server", anchor::keyspace(path_of("cluster")))));

    // The first primary key is accepted: it is THE slice selector of the space.
    REQUIRE(reg.attach(
        nucleus::primary_key_element("name", anchor::keyspace(path_of("cluster/server")))));

    // A second primary key under the SAME container is rejected at attach -- two
    // selectors would make a slice ambiguous.
    auto second = reg.attach(
        nucleus::primary_key_element("id", anchor::keyspace(path_of("cluster/server"))));
    REQUIRE_FALSE(second);
    REQUIRE(second.error().find("already the config space's primary key")
            != std::string::npos);

    // A primary key under a DIFFERENT container is rejected just the same: the
    // primary key is singular per config space, not per container.
    auto elsewhere = reg.attach(
        nucleus::primary_key_element("name", anchor::keyspace(path_of("cluster"))));
    REQUIRE_FALSE(elsewhere);
    REQUIRE(elsewhere.error().find("already the config space's primary key")
            != std::string::npos);

    // Unique fields remain freely attachable alongside the primary key.
    REQUIRE(reg.attach(
        nucleus::unique_element("serial", anchor::keyspace(path_of("cluster/server")))));
}
