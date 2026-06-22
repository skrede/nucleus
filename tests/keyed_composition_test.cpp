#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>

// KCM-01..05: cross-layer merge modes on a repeated/identified collection. Domain-neutral
// schema: endpoints/output[name] with an addr leaf. Two stacked runtime layers (the second
// is the higher-precedence override).

using namespace nucleus;

namespace {

// endpoints/output (repeated, `mode`) keyed by `name` via an identity group.
config_space make_space(merge_mode mode, bool with_identity = true)
{
    config_space_builder b;
    REQUIRE(b.register_element(element("endpoints", anchor::root())));
    REQUIRE(b.register_element(
        merging(repeated_element("output", anchor::keyspace("endpoints")), mode)));
    REQUIRE(b.register_element(element("name", anchor::keyspace("endpoints/output"))));
    REQUIRE(b.register_element(element("addr", anchor::keyspace("endpoints/output"))));
    if(with_identity)
        REQUIRE(b.register_identity_group(
            identity_group("output_names", anchor::keyspace("endpoints"))
                .members({"output"}).field("name")));
    return std::move(b).build();
}

std::vector<std::string> names(const config &cfg)
{
    return cfg.get_all("endpoints/output/name");
}

bool mentions(const error &e, const char *needle)
{
    return e.message.find(needle) != std::string::npos;
}

}

TEST_CASE("KCM-01: wholesale_replace (default) — higher layer replaces the whole collection",
          "[keyed][KCM-01]")
{
    auto space = make_space(merge_mode::wholesale_replace, /*with_identity=*/false);

    runtime_source base;
    base.set("endpoints/output[0]/name", "a").set("endpoints/output[1]/name", "b");
    runtime_source over;
    over.set("endpoints/output[0]/name", "c");

    auto r = load_config(space, source_stack{std::move(base), std::move(over)}, {});
    REQUIRE(r.has_value());
    auto n = names(*r);
    REQUIRE(n == std::vector<std::string>{"c"});  // base {a,b} replaced wholesale
}

TEST_CASE("KCM-02: unite — layers union; a duplicate identifier across layers is loud",
          "[keyed][KCM-02]")
{
    SECTION("base + override union into one collection")
    {
        auto space = make_space(merge_mode::unite);
        runtime_source base;
        base.set("endpoints/output[0]/name", "a").set("endpoints/output[1]/name", "b");
        runtime_source over;
        over.set("endpoints/output[0]/name", "c");

        auto r = load_config(space, source_stack{std::move(base), std::move(over)}, {});
        REQUIRE(r.has_value());
        auto n = names(*r);
        std::sort(n.begin(), n.end());
        REQUIRE(n == std::vector<std::string>{"a", "b", "c"});
    }
    SECTION("a duplicate identifier across layers is a strict-additive error")
    {
        auto space = make_space(merge_mode::unite);
        runtime_source base;
        base.set("endpoints/output[0]/name", "a");
        runtime_source over;
        over.set("endpoints/output[0]/name", "a");  // same key, higher layer

        auto r = load_config(space, source_stack{std::move(base), std::move(over)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "strict-additive"));
        REQUIRE(mentions(r.error(), "'a'"));
    }
}

TEST_CASE("KCM-03: replace_by_key — a matching identifier replaces the whole element",
          "[keyed][KCM-03]")
{
    auto space = make_space(merge_mode::replace_by_key);
    runtime_source base;
    base.set("endpoints/output[0]/name", "a").set("endpoints/output[0]/addr", "base-a")
        .set("endpoints/output[1]/name", "b").set("endpoints/output[1]/addr", "base-b");
    runtime_source over;
    over.set("endpoints/output[0]/name", "b").set("endpoints/output[0]/addr", "over-b")
        .set("endpoints/output[1]/name", "c").set("endpoints/output[1]/addr", "over-c");

    auto r = load_config(space, source_stack{std::move(base), std::move(over)}, {});
    REQUIRE(r.has_value());
    auto n = names(*r);
    std::sort(n.begin(), n.end());
    REQUIRE(n == std::vector<std::string>{"a", "b", "c"});  // a kept, b replaced, c added

    // b's whole element comes from the override layer (addr = over-b, not base-b).
    auto addrs = r->get_all("endpoints/output/addr");
    REQUIRE(std::find(addrs.begin(), addrs.end(), "over-b") != addrs.end());
    REQUIRE(std::find(addrs.begin(), addrs.end(), "base-b") == addrs.end());
}

TEST_CASE("KCM-04: a keyed mode without an identity group is a loud error", "[keyed][KCM-04]")
{
    auto space = make_space(merge_mode::unite, /*with_identity=*/false);
    runtime_source src;
    src.set("endpoints/output[0]/name", "a");
    auto r = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(mentions(r.error(), "no identity group"));
}

TEST_CASE("KCM-03: keyed merge composes with strain slicing (nested in a pkey strain)",
          "[keyed][KCM-03]")
{
    config_space_builder b;
    REQUIRE(b.register_element(element("cluster", anchor::root())));
    REQUIRE(b.register_element(element("server", anchor::keyspace("cluster"))));
    REQUIRE(b.register_element(primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(b.register_element(
        merging(repeated_element("output", anchor::keyspace("cluster/server")),
                merge_mode::unite)));
    REQUIRE(b.register_element(element("id", anchor::keyspace("cluster/server/output"))));
    REQUIRE(b.register_identity_group(
        identity_group("output_ids", anchor::keyspace("cluster/server"))
            .members({"output"}).field("id")));
    auto space = std::move(b).build();

    runtime_source base;
    runtime_source over;

    SECTION("union within the selected strain")
    {
        base.set("cluster/server/web/output[0]/id", "x");
        over.set("cluster/server/web/output[0]/id", "y");
        load_options opt;
        opt.selection = "web";
        auto r = load_config(space, source_stack{std::move(base), std::move(over)}, opt);
        REQUIRE(r.has_value());
        auto ids = r->get_all("cluster/server/output/id");
        std::sort(ids.begin(), ids.end());
        REQUIRE(ids == std::vector<std::string>{"x", "y"});
    }
    SECTION("a sibling strain's keyed collection does not leak into the selection")
    {
        base.set("cluster/server/web/output[0]/id", "x")
            .set("cluster/server/api/output[0]/id", "p");  // a different strain
        over.set("cluster/server/web/output[0]/id", "y");
        load_options opt;
        opt.selection = "web";
        auto r = load_config(space, source_stack{std::move(base), std::move(over)}, opt);
        REQUIRE(r.has_value());
        auto ids = r->get_all("cluster/server/output/id");
        std::sort(ids.begin(), ids.end());
        REQUIRE(ids == std::vector<std::string>{"x", "y"});  // 'p' (api strain) excluded
    }
}
