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

TEST_CASE("identity is enforced separately from required", "[enforcer]")
{
    schema_registry reg;
    reg.attach(nucleus::element("node", anchor::root()));
    // An identity field that is NOT marked required.
    reg.attach(nucleus::identity_element("name", anchor::keyspace(path_of("node"))));

    keyspace ks; // name absent

    auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    // The failure is reported as an identity/selector failure, distinct from the
    // required-field wording -- proving the two constraints are separate.
    REQUIRE(violation_mentions(v.error(), "identity field"));
    REQUIRE_FALSE(violation_mentions(v.error(), "required field"));
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
