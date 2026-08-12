#include "collection_shapes.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/config_space.h"

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

bool mentions(const std::vector<schema_violation> &vs, const char *needle)
{
    return std::any_of(vs.begin(), vs.end(), [&](const schema_violation &v)
                       { return v.reason.find(needle) != std::string::npos; });
}

// cluster / node[] / route[] / port, port unique. The routes of one node are the
// sibling set the value competes in; two nodes are unrelated outer parents.
schema_registry unique_port_under_routes()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(kp("cluster")))));
    REQUIRE(reg.attach(
        nucleus::repeated_element("route", anchor::keyspace(kp("cluster/node")))));
    REQUIRE(reg.attach(
        nucleus::unique_element("port", anchor::keyspace(kp("cluster/node/route")))));
    return reg;
}

keyspace ports(const std::vector<std::pair<const char *, const char *>> &entries)
{
    keyspace ks;
    for(const auto &[path, text] : entries)
        ks.set(kp(path), nucleus::value::owned(text));
    return ks;
}

nucleus::config_space route_port_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("route", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::unique_element("port", anchor::keyspace("cluster/node/route"))));
    return builder.build();
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

}

TEST_CASE("two node instances each carrying a route on the same port validate cleanly",
          "[collection_shapes][unique]")
{
    const keyspace ks = ports({{"cluster/node[0]/route[0]/port", "8080"},
                               {"cluster/node[1]/route[0]/port", "8080"}});

    REQUIRE(schema_enforcer::validate(unique_port_under_routes(), ks));
}

TEST_CASE("two routes of one node on the same port are rejected, naming both routes",
          "[collection_shapes][unique]")
{
    const keyspace ks = ports({{"cluster/node[0]/route[0]/port", "8080"},
                               {"cluster/node[0]/route[1]/port", "8080"}});

    const auto v = schema_enforcer::validate(unique_port_under_routes(), ks);
    REQUIRE_FALSE(v);
    REQUIRE(v.error().size() == 1);
    REQUIRE(v.error().front().path == "cluster/node[0]/route[0]/port");
    REQUIRE(v.error().front().reason
            == "unique field 'cluster/node/route/port' has duplicate value '8080' "
               "across sibling instances 'cluster/node[0]/route[0]/port', "
               "'cluster/node[0]/route[1]/port'");
}

TEST_CASE("a duplicate inside one node is reported while a sibling node reusing the "
          "value is not",
          "[collection_shapes][unique]")
{
    const keyspace ks = ports({{"cluster/node[0]/route[0]/port", "80"},
                               {"cluster/node[0]/route[1]/port", "80"},
                               {"cluster/node[1]/route[0]/port", "80"},
                               {"cluster/node[1]/route[1]/port", "443"}});

    const auto v = schema_enforcer::validate(unique_port_under_routes(), ks);
    REQUIRE_FALSE(v);
    REQUIRE(v.error().size() == 1);
    REQUIRE(mentions(v.error(), "cluster/node[0]/route[0]/port"));
    REQUIRE(mentions(v.error(), "cluster/node[0]/route[1]/port"));
    REQUIRE_FALSE(mentions(v.error(), "cluster/node[1]/route[0]/port"));
}

TEST_CASE("ports distinct within each node validate even when the values repeat "
          "across nodes",
          "[collection_shapes][unique]")
{
    const keyspace ks = ports({{"cluster/node[0]/route[0]/port", "80"},
                               {"cluster/node[0]/route[1]/port", "443"},
                               {"cluster/node[1]/route[0]/port", "80"},
                               {"cluster/node[1]/route[1]/port", "443"}});

    REQUIRE(schema_enforcer::validate(unique_port_under_routes(), ks));
}

TEST_CASE("a document whose two nodes each carry a route on the same port loads",
          "[collection_shapes][unique]")
{
    const auto loaded = nucleus::load_config(route_port_space(),
        nucleus::source_stack{xml_of("<cluster>"
                                     "<node><route><port>8080</port></route></node>"
                                     "<node><route><port>8080</port></route></node>"
                                     "</cluster>")},
        {});
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));
    REQUIRE(loaded.value().get("cluster/node[1]/route[0]/port") == "8080");
}

TEST_CASE("a document whose one node carries two routes on the same port is rejected",
          "[collection_shapes][unique]")
{
    const auto loaded = nucleus::load_config(route_port_space(),
        nucleus::source_stack{xml_of("<cluster><node>"
                                     "<route><port>8080</port></route>"
                                     "<route><port>8080</port></route>"
                                     "</node></cluster>")},
        {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message.find("cluster/node[0]/route[0]/port")
            != std::string::npos);
    REQUIRE(loaded.error().message.find("cluster/node[0]/route[1]/port")
            != std::string::npos);
}
