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
using nucleus::schema_violation;

namespace {

key_path kp(const char *text) { return key_path::parse(text).value(); }

bool names(const std::vector<schema_violation> &vs, const char *path)
{
    return std::any_of(vs.begin(), vs.end(),
                       [&](const schema_violation &v) { return v.path == path; });
}

// cluster / node[] / { port (required), label }.
schema_registry required_port_under_nodes()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(kp("cluster")))));
    REQUIRE(reg.attach(nucleus::required_element("port", anchor::keyspace(kp("cluster/node")))));
    REQUIRE(reg.attach(nucleus::element("label", anchor::keyspace(kp("cluster/node")))));
    return reg;
}

}

TEST_CASE("a required child missing from one instance is reported for that instance",
          "[collection_shapes][presence]")
{
    const schema_registry reg = required_port_under_nodes();

    keyspace ks;
    ks.set(kp("cluster/node[0]/port"), nucleus::value::owned("80"));
    ks.set(kp("cluster/node[1]/label"), nucleus::value::owned("second"));

    const auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    REQUIRE(names(v.error(), "cluster/node[1]/port"));
    REQUIRE_FALSE(names(v.error(), "cluster/node[0]/port"));
    REQUIRE(v.error().front().reason
            == "required field 'cluster/node[1]/port' is missing");
}

TEST_CASE("three instances with two missing the required child yield two violations",
          "[collection_shapes][presence]")
{
    const schema_registry reg = required_port_under_nodes();

    keyspace ks;
    ks.set(kp("cluster/node[0]/port"), nucleus::value::owned("80"));
    ks.set(kp("cluster/node[1]/label"), nucleus::value::owned("b"));
    ks.set(kp("cluster/node[2]/label"), nucleus::value::owned("c"));

    const auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    REQUIRE(v.error().size() == 2);
    REQUIRE(names(v.error(), "cluster/node[1]/port"));
    REQUIRE(names(v.error(), "cluster/node[2]/port"));
}

TEST_CASE("a required child under nested repetition is checked per innermost instance",
          "[collection_shapes][presence]")
{
    // cluster / node[] / tags[] / name (required): four inner instances across two
    // outer ones, and only the last omits its name.
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(kp("cluster")))));
    REQUIRE(reg.attach(nucleus::repeated_element("tags", anchor::keyspace(kp("cluster/node")))));
    REQUIRE(reg.attach(
        nucleus::required_element("name", anchor::keyspace(kp("cluster/node/tags")))));
    REQUIRE(reg.attach(nucleus::element("kind", anchor::keyspace(kp("cluster/node/tags")))));

    keyspace ks;
    ks.set(kp("cluster/node[0]/tags[0]/name"), nucleus::value::owned("a"));
    ks.set(kp("cluster/node[0]/tags[1]/name"), nucleus::value::owned("b"));
    ks.set(kp("cluster/node[1]/tags[0]/name"), nucleus::value::owned("c"));
    ks.set(kp("cluster/node[1]/tags[1]/kind"), nucleus::value::owned("soft"));

    const auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    REQUIRE(v.error().size() == 1);
    REQUIRE(v.error().front().path == "cluster/node[1]/tags[1]/name");
}

TEST_CASE("a required element with no repeated ancestor names the declared path",
          "[collection_shapes][presence]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("plexus", anchor::root())));
    REQUIRE(reg.attach(nucleus::required_element("port", anchor::keyspace(kp("plexus")))));
    REQUIRE(reg.attach(nucleus::element("label", anchor::keyspace(kp("plexus")))));

    keyspace ks;
    ks.set(kp("plexus/label"), nucleus::value::owned("x"));

    const auto v = schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(v);
    REQUIRE(v.error().size() == 1);
    REQUIRE(v.error().front().path == "plexus/port");
    REQUIRE(v.error().front().reason == "required field 'plexus/port' is missing");
}

TEST_CASE("a repeated container resolving to zero instances emits no child violation",
          "[collection_shapes][presence]")
{
    const schema_registry reg = required_port_under_nodes();

    const keyspace empty;
    REQUIRE(schema_enforcer::validate(reg, empty));
}

TEST_CASE("a child cannot be declared under a required element",
          "[collection_shapes][presence]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    auto node = nucleus::repeated_element("node", anchor::keyspace(kp("cluster")));
    node.required = true;
    REQUIRE(reg.attach(std::move(node)));

    const auto refused =
        reg.attach(nucleus::required_element("port", anchor::keyspace(kp("cluster/node"))));
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().find("'port'") != std::string::npos);
    REQUIRE(refused.error().find("'cluster/node'") != std::string::npos);
}

TEST_CASE("a primary key cannot be declared under a repeated container",
          "[collection_shapes][presence]")
{
    // The registry rejects a primary key beneath any repeated ancestor, so the
    // structural satisfaction of a required identity element is reachable only on
    // the declared path -- never per instance.
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(kp("cluster")))));
    auto id = nucleus::identity_element("name", anchor::keyspace(kp("cluster/node")));
    id.required = true;
    REQUIRE_FALSE(reg.attach(std::move(id)));
}

TEST_CASE("a required identity on a plain container is satisfied by a sliced strain",
          "[collection_shapes][presence]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("node", anchor::keyspace(kp("cluster")))));
    auto id = nucleus::identity_element("name", anchor::keyspace(kp("cluster/node")));
    id.required = true;
    REQUIRE(reg.attach(std::move(id)));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(kp("cluster/node")))));

    keyspace ks;
    ks.set(kp("cluster/node/port"), nucleus::value::owned("80"));

    REQUIRE_FALSE(schema_enforcer::validate(reg, ks));
    REQUIRE(schema_enforcer::validate(reg, ks, {"cluster/node"}));
}
