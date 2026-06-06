#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/source/argv/argv_source.h"
#include "nucleus/source/argv/cli_surface.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>

using nucleus::anchor;
using nucleus::keyspace;
using nucleus::key_path;
using nucleus::argv_source;
using nucleus::schema_registry;
using nucleus::schema_enforcer;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

schema_registry build_schema()
{
    schema_registry reg;
    reg.attach(nucleus::element("plexus", anchor::root()));
    reg.attach(nucleus::element("udp", anchor::keyspace(path_of("plexus"))));
    reg.attach(nucleus::required_element(
        "auth_mode", anchor::keyspace(path_of("plexus/udp"))));
    return reg;
}

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
