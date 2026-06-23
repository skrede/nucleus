#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Identity (key) groups. A namespace pools one identifier field across
// the instances of several repeated member element-types under one parent container,
// required present and unique within a slice. Domain-neutral: a `pool` of `worker`
// and `gateway` element-types, each identified by `name`.

using namespace nucleus;

namespace {

config_space_builder pool_builder()
{
    config_space_builder b;
    REQUIRE(b.register_element(element("pool", anchor::root())));
    REQUIRE(b.register_element(repeated_element("worker", anchor::keyspace("pool"))));
    REQUIRE(b.register_element(element("name", anchor::keyspace("pool/worker"))));
    REQUIRE(b.register_element(element("port", anchor::keyspace("pool/worker"))));
    REQUIRE(b.register_element(repeated_element("gateway", anchor::keyspace("pool"))));
    REQUIRE(b.register_element(element("name", anchor::keyspace("pool/gateway"))));
    REQUIRE(b.register_element(element("port", anchor::keyspace("pool/gateway"))));
    return b;
}

identity_group_spec pool_names()
{
    return identity_group("component_names", anchor::keyspace("pool"))
        .members({"worker", "gateway"}).field("name");
}

bool mentions(const error &e, const char *needle)
{
    return e.message.find(needle) != std::string::npos;
}

}

TEST_CASE("Distinct identifiers across pooled element-types validate", "[identity]")
{
    auto b = pool_builder();
    REQUIRE(b.register_identity_group(pool_names()));
    auto space = std::move(b).build();

    runtime_source src;
    src.set("pool/worker[0]/name", "a").set("pool/worker[0]/port", "1")
       .set("pool/gateway[0]/name", "b").set("pool/gateway[0]/port", "2");
    REQUIRE(load_config(space, source_stack{std::move(src)}, {}).has_value());
}

TEST_CASE("A duplicate identifier value alone is a collision", "[identity]")
{
    SECTION("same value across two element-types names both")
    {
        auto b = pool_builder();
        REQUIRE(b.register_identity_group(pool_names()));
        auto space = std::move(b).build();

        runtime_source src;
        src.set("pool/worker[0]/name", "shared").set("pool/worker[0]/port", "1")
           .set("pool/gateway[0]/name", "shared").set("pool/gateway[0]/port", "2");
        auto r = load_config(space, source_stack{std::move(src)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "not unique"));
        REQUIRE(mentions(r.error(), "'shared'"));
        REQUIRE(mentions(r.error(), "worker"));
        REQUIRE(mentions(r.error(), "gateway"));
    }
    SECTION("same value within one element-type collides too")
    {
        auto b = pool_builder();
        REQUIRE(b.register_identity_group(pool_names()));
        auto space = std::move(b).build();

        runtime_source src;
        src.set("pool/worker[0]/name", "dup").set("pool/worker[0]/port", "1")
           .set("pool/worker[1]/name", "dup").set("pool/worker[1]/port", "2");
        auto r = load_config(space, source_stack{std::move(src)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "not unique"));
    }
}

TEST_CASE("An instance missing its identifier field is loud (xs:key present)",
          "[identity]")
{
    auto b = pool_builder();
    REQUIRE(b.register_identity_group(pool_names()));
    auto space = std::move(b).build();

    runtime_source src;
    src.set("pool/worker[0]/port", "1");  // no name
    auto r = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(mentions(r.error(), "missing its identifier field"));
}

TEST_CASE("Uniqueness scope is the resolved slice (per selected strain)",
          "[identity]")
{
    // A pkey container `cluster/server[name]` slices to one strain; each strain
    // owns an independent `pool`, so the same component name may recur across strains.
    config_space_builder b;
    REQUIRE(b.register_element(element("cluster", anchor::root())));
    REQUIRE(b.register_element(element("server", anchor::keyspace("cluster"))));
    REQUIRE(b.register_element(primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(b.register_element(element("pool", anchor::keyspace("cluster/server"))));
    REQUIRE(b.register_element(repeated_element("worker", anchor::keyspace("cluster/server/pool"))));
    REQUIRE(b.register_element(element("id", anchor::keyspace("cluster/server/pool/worker"))));
    REQUIRE(b.register_identity_group(
        identity_group("worker_ids", anchor::keyspace("cluster/server/pool"))
            .members({"worker"}).field("id")));
    auto space = std::move(b).build();

    runtime_source src;
    src.set("cluster/server/web/pool/worker[0]/id", "w1")
       .set("cluster/server/api/pool/worker[0]/id", "w1");  // same id, different strain
    load_options opt;
    opt.selection = "web";
    auto r = load_config(space, source_stack{std::move(src)}, opt);
    // Only the `web` strain survives the slice, so "w1" is unique within it.
    REQUIRE(r.has_value());
}

TEST_CASE("A reserved namespace name is rejected at registration", "[identity]")
{
    SECTION("a builtin tokenizer category")
    {
        auto b = pool_builder();
        auto reg = b.register_identity_group(
            identity_group("env", anchor::keyspace("pool")).members({"worker"}).field("name"));
        REQUIRE_FALSE(reg.has_value());
        REQUIRE(reg.error().message.find("reserved") != std::string::npos);
    }
    SECTION("the engine's own nucleus prefix")
    {
        auto b = pool_builder();
        auto reg = b.register_identity_group(
            identity_group("nucleus_ids", anchor::keyspace("pool")).members({"worker"}).field("name"));
        REQUIRE_FALSE(reg.has_value());
    }
}
