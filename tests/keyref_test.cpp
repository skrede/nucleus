#include "nucleus/query/query.h"
#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// KRF-01..03: schema-declared references by identifier (the xs:keyref analog). A
// `route/target` keyref points into the `endpoint_names` namespace, which pools the
// `name` of the `output` and `endpoint` element-types under `endpoints`.

using namespace nucleus;

namespace {

config_space make_space()
{
    config_space_builder b;
    REQUIRE(b.register_element(element("endpoints", anchor::root())));
    REQUIRE(b.register_element(repeated_element("output", anchor::keyspace("endpoints"))));
    REQUIRE(b.register_element(element("name", anchor::keyspace("endpoints/output"))));
    REQUIRE(b.register_element(repeated_element("endpoint", anchor::keyspace("endpoints"))));
    REQUIRE(b.register_element(element("name", anchor::keyspace("endpoints/endpoint"))));
    REQUIRE(b.register_identity_group(
        identity_group("endpoint_names", anchor::keyspace("endpoints"))
            .members({"output", "endpoint"}).field("name")));
    REQUIRE(b.register_element(element("route", anchor::root())));
    REQUIRE(b.register_element(
        keyref_element("target", anchor::keyspace("route"), "endpoint_names")));
    return std::move(b).build();
}

runtime_source two_targets()
{
    runtime_source s;
    s.set("endpoints/output[0]/name", "alpha")
     .set("endpoints/endpoint[0]/name", "beta");
    return s;
}

bool mentions(const error &e, const char *needle)
{
    return e.message.find(needle) != std::string::npos;
}

}

TEST_CASE("KRF-01: a keyref into an unregistered namespace is a registration error",
          "[keyref][KRF-01]")
{
    config_space_builder b;
    REQUIRE(b.register_element(element("route", anchor::root())));
    auto reg = b.register_element(
        keyref_element("target", anchor::keyspace("route"), "nonsuch"));
    REQUIRE_FALSE(reg.has_value());
    REQUIRE(reg.error().message.find("nonsuch") != std::string::npos);
}

TEST_CASE("KRF-02: a valid keyref validates; a dangling one is loud with a did-you-mean",
          "[keyref][KRF-02]")
{
    auto space = make_space();

    SECTION("a value naming a real identifier validates")
    {
        runtime_source s = two_targets();
        s.set("route/target", "alpha");
        REQUIRE(load_config(space, source_stack{std::move(s)}, {}).has_value());
    }
    SECTION("a value naming no identifier is a dangling-reference error")
    {
        runtime_source s = two_targets();
        s.set("route/target", "alpro");  // near-miss of "alpha"
        auto r = load_config(space, source_stack{std::move(s)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "matches no identifier"));
        REQUIRE(mentions(r.error(), "endpoint_names"));
        REQUIRE(mentions(r.error(), "did you mean 'alpha'"));
    }
    SECTION("an absent keyref is not a dangling reference (orthogonal to required)")
    {
        runtime_source s = two_targets();  // route/target unset
        REQUIRE(load_config(space, source_stack{std::move(s)}, {}).has_value());
    }
}

TEST_CASE("KRF-03: follow_keyref dereferences to the target node", "[keyref][KRF-03]")
{
    auto space = make_space();
    const auto ctx = space.query_context();

    SECTION("resolves to a target in the first element-type")
    {
        runtime_source s = two_targets();
        s.set("route/target", "alpha");
        auto cfg = load_config(space, source_stack{std::move(s)}, {});
        REQUIRE(cfg.has_value());

        config_node keyref = cfg->root()["route"]["target"];
        auto target = follow_keyref(keyref, ctx);
        REQUIRE(target.has_value());
        REQUIRE(target->path() == "endpoints/output[0]");
        REQUIRE(target->operator[]("name").value() == "alpha");
    }
    SECTION("resolves across element-types to the endpoint pool")
    {
        runtime_source s = two_targets();
        s.set("route/target", "beta");
        auto cfg = load_config(space, source_stack{std::move(s)}, {});
        REQUIRE(cfg.has_value());

        config_node keyref = cfg->root()["route"]["target"];
        auto target = follow_keyref(keyref, ctx);
        REQUIRE(target.has_value());
        REQUIRE(target->path() == "endpoints/endpoint[0]");
    }
    SECTION("a node that is not a keyref yields absent_key")
    {
        runtime_source s = two_targets();
        s.set("route/target", "alpha");
        auto cfg = load_config(space, source_stack{std::move(s)}, {});
        REQUIRE(cfg.has_value());

        config_node not_keyref = cfg->root()["endpoints"]["output"][std::size_t{0}]["name"];
        auto target = follow_keyref(not_keyref, ctx);
        REQUIRE_FALSE(target.has_value());
        REQUIRE(target.error().code == errc::absent_key);
    }
}
