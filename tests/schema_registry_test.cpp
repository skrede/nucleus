#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"
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

TEST_CASE("digit-led element name rejected at attach", "[schema_registry][digit_led]")
{
    schema_registry reg;

    // Names starting with a digit are rejected: CLI flag disambiguation requires names
    // that are invertible from flag text, and digits at the front break that rule.
    auto r0 = reg.attach(nucleus::element("0tag", anchor::root()));
    REQUIRE_FALSE(r0);
    REQUIRE(r0.error().find("digit-led name") != std::string::npos);

    auto r1 = reg.attach(nucleus::element("1robot", anchor::root()));
    REQUIRE_FALSE(r1);
    REQUIRE(r1.error().find("digit-led name") != std::string::npos);

    // A digit anywhere except the front is fine.
    REQUIRE(reg.attach(nucleus::element("tag", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("t0ag", anchor::keyspace(path_of("tag")))));
}

TEST_CASE("canonical_text strips indexed segments", "[schema_registry][canonical_text][indexed]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace("cluster/node"))));
    REQUIRE(reg.attach(nucleus::element("endpoint", anchor::keyspace("cluster/node"))));

    REQUIRE(reg.attach(nucleus::element("config", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("tags", anchor::keyspace("config"))));

    SECTION("indexed container child -> stripped canonical")
    {
        auto p = key_path::parse("cluster/node[0]/port").value();
        REQUIRE(reg.canonical_text(p) == "cluster/node/port");
    }

    SECTION("deeper indexed child -> all ordinal segments stripped")
    {
        auto p = key_path::parse("cluster/node[1]/endpoint").value();
        REQUIRE(reg.canonical_text(p) == "cluster/node/endpoint");
    }

    SECTION("repeated leaf indexed path -> base-name canonical")
    {
        auto p = key_path::parse("config/tags[0]").value();
        REQUIRE(reg.canonical_text(p) == "config/tags");
    }

    SECTION("container-level indexed path -> container canonical")
    {
        auto p = key_path::parse("cluster/node[0]").value();
        REQUIRE(reg.canonical_text(p) == "cluster/node");
    }

    SECTION("non-indexed path unchanged")
    {
        auto p = key_path::parse("cluster/node/port").value();
        REQUIRE(reg.canonical_text(p) == "cluster/node/port");
    }
}

TEST_CASE("Primary key inside repeated container rejected at attach",
          "[schema_registry][repeated_pkey]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::repeated_element("link", anchor::root())));

    // A primary key nested under a repeated container is rejected: keyed selection
    // has no clean per-instance meaning inside a repeated (ordinal) container.
    auto bad = reg.attach(
        nucleus::identity_element("id", anchor::keyspace(path_of("link"))));
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().find("primary key under repeated") != std::string::npos);

    // Other fields under the same repeated container are perfectly admissible.
    REQUIRE(reg.attach(
        nucleus::required_element("mass", anchor::keyspace(path_of("link")))));
    REQUIRE(reg.attach(
        nucleus::element("length", anchor::keyspace(path_of("link")))));
}

TEST_CASE("Primary key under non-repeated child of repeated container rejected",
          "[schema_registry][repeated_pkey][CR02]")
{
    // Schema: cluster -> node (repeated) -> details (NOT repeated) -> name (identity)
    // The transitive-ancestor check must catch this: 'name' is under 'details',
    // which is not repeated, but 'node' is a repeated ancestor.
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::element("details", anchor::keyspace("cluster/node"))));

    // Identity element under details -- rejected because node is a repeated ancestor.
    auto bad = reg.attach(
        nucleus::identity_element("name", anchor::keyspace("cluster/node/details")));
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().find("primary key under repeated") != std::string::npos);
    // The ancestor 'cluster/node' must be named in the error.
    REQUIRE(bad.error().find("cluster/node") != std::string::npos);
}

TEST_CASE("re-declaring an already-declared element path is rejected at attach",
          "[schema_registry][redeclaration]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));

    // The first declaration at cluster/node wins.
    REQUIRE(reg.attach(nucleus::element("node", anchor::keyspace("cluster"))));

    // Redeclaring the same exact path as a primary key is rejected -- otherwise a
    // pkey could be smuggled under a container by declaration order. The reject is
    // what keeps attach-time invariants independent of declaration order.
    auto as_identity =
        reg.attach(nucleus::identity_element("node", anchor::keyspace("cluster")));
    REQUIRE_FALSE(as_identity);
    REQUIRE(as_identity.error().find("re-declares") != std::string::npos);

    // Redeclaring the same exact path as a repeated container is rejected too.
    auto as_repeated =
        reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster")));
    REQUIRE_FALSE(as_repeated);
    REQUIRE(as_repeated.error().find("re-declares") != std::string::npos);
}

TEST_CASE("re-declaration reject is order-independent",
          "[schema_registry][redeclaration]")
{
    // The same two declarations attached in the opposite order still leave the
    // first winner in place and reject the second -- the schema's meaning cannot
    // depend on which declaration is attached first.
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));

    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    auto second =
        reg.attach(nucleus::element("node", anchor::keyspace("cluster")));
    REQUIRE_FALSE(second);
    REQUIRE(second.error().find("re-declares") != std::string::npos);
}

TEST_CASE("a second typed element at one path is rejected, not silently dropped",
          "[schema_registry][redeclaration]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("logging", anchor::root())));

    REQUIRE(reg.attach(
        nucleus::typed_element<int>("level", anchor::keyspace("logging"))));

    // A second typed element at the same exact path used to be silently discarded
    // at the typed store; it is now rejected loudly.
    auto second = reg.attach(
        nucleus::typed_element<double>("level", anchor::keyspace("logging")));
    REQUIRE_FALSE(second);
    REQUIRE(second.error().find("re-declares") != std::string::npos);
}

TEST_CASE("a legitimate child attach under a declared container still succeeds",
          "[schema_registry][redeclaration]")
{
    // The reject is exact-path membership, never prefix membership: an already
    // declared container path is a prefix of its children, and every child must
    // still attach.
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("node", anchor::keyspace("cluster"))));

    // Children under the declared container are not re-declarations of it.
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace("cluster/node"))));
    REQUIRE(reg.attach(nucleus::element("host", anchor::keyspace("cluster/node"))));
    REQUIRE(reg.recognizes(path_of("cluster/node/port")));
    REQUIRE(reg.recognizes(path_of("cluster/node/host")));
}
