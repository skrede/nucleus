#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>
#include <algorithm>

using nucleus::anchor;
using nucleus::keyspace;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::schema_enforcer;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

bool violation_mentions(const std::vector<nucleus::schema_violation> &vs,
                        const char *needle)
{
    return std::any_of(vs.begin(), vs.end(), [&](const nucleus::schema_violation &v)
                       { return v.reason.find(needle) != std::string::npos; });
}

}

TEST_CASE("a resolved keyspace satisfying the schema validates", "[enforcer]")
{
    schema_registry reg;
    reg.attach(nucleus::element("plexus", anchor::root()));
    reg.attach(nucleus::required_element("port", anchor::keyspace(path_of("plexus"))));

    keyspace ks;
    ks.set(path_of("plexus/port"), nucleus::value::owned("8080"));

    REQUIRE(schema_enforcer::validate(reg, ks));
}

TEST_CASE("a missing required field is a violation", "[enforcer]")
{
    schema_registry reg;
    reg.attach(nucleus::element("plexus", anchor::root()));
    reg.attach(nucleus::required_element("port", anchor::keyspace(path_of("plexus"))));

    keyspace ks; // port absent

    auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    REQUIRE(violation_mentions(v.error(), "required field"));
}

TEST_CASE("identity alone imposes no presence obligation", "[enforcer]")
{
    schema_registry reg;
    reg.attach(nucleus::element("node", anchor::root()));
    reg.attach(nucleus::identity_element("name", anchor::keyspace(path_of("node"))));
    reg.attach(nucleus::element("role", anchor::keyspace(path_of("node"))));

    // An empty keyspace validates: a space with no strain at all is legal.
    keyspace ks;
    REQUIRE(schema_enforcer::validate(reg, ks));

    // So does an anonymous strain -- fields without the key collapse into the
    // configuration space; the primary key is a selector, not an obligation.
    ks.set(path_of("node/role"), nucleus::value::owned("primary"));
    REQUIRE(schema_enforcer::validate(reg, ks));
}

TEST_CASE("a required identity demands a named strain", "[enforcer]")
{
    schema_registry reg;
    reg.attach(nucleus::element("node", anchor::root()));
    nucleus::schema_element id =
        nucleus::identity_element("name", anchor::keyspace(path_of("node")));
    id.required = true;
    reg.attach(std::move(id));
    reg.attach(nucleus::element("role", anchor::keyspace(path_of("node"))));

    // Anonymous-only content violates: the host required a NAMED strain. The
    // failure speaks the required-field vocabulary -- identity adds no separate
    // presence check.
    keyspace ks;
    ks.set(path_of("node/role"), nucleus::value::owned("primary"));
    auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    REQUIRE(violation_mentions(v.error(), "required field"));

    // A sliced strain satisfies it structurally: the key value named the
    // instance and was consumed, so no literal leaf can exist.
    REQUIRE(schema_enforcer::validate(reg, ks, {"node"}));
}

TEST_CASE("a value within a declared allowed set passes", "[enforcer]")
{
    schema_registry reg;
    reg.attach(nucleus::element("logging", anchor::root()));
    reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("logging")),
                                     {"debug", "info", "warn", "error"}));

    keyspace ks;
    ks.set(path_of("logging/level"), nucleus::value::owned("warn"));

    REQUIRE(schema_enforcer::validate(reg, ks));
}

TEST_CASE("a value outside the declared allowed set is rejected", "[enforcer]")
{
    schema_registry reg;
    reg.attach(nucleus::element("logging", anchor::root()));
    reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("logging")),
                                     {"debug", "info", "warn", "error"}));

    keyspace ks;
    ks.set(path_of("logging/level"), nucleus::value::owned("warm"));

    auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    REQUIRE(violation_mentions(v.error(), "not one of the allowed values"));
    // The nearest allowed value is suggested.
    REQUIRE(violation_mentions(v.error(), "did you mean 'warn'?"));
}

TEST_CASE("a value at an undeclared path is rejected", "[enforcer]")
{
    schema_registry reg;
    reg.attach(nucleus::element("plexus", anchor::root()));
    reg.attach(nucleus::element("port", anchor::keyspace(path_of("plexus"))));

    keyspace ks;
    ks.set(path_of("plexus/port"), nucleus::value::owned("8080"));
    ks.set(path_of("plexus/bogus"), nucleus::value::owned("x"));

    auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    REQUIRE(violation_mentions(v.error(), "not declared by the schema"));
}
