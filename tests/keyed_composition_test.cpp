#include "builder_result_test_support.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/cli_surface.h"

#include "nucleus/env/env_source.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>

// cross-layer merge modes on a repeated/identified collection. Domain-neutral
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
    return nucleus::builder_result_test::built(std::move(b));
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

TEST_CASE("replace_by_ordinal (default) — higher layer replaces the instances it supplies",
          "[keyed]")
{
    auto space = make_space(merge_mode::replace_by_ordinal, /*with_identity=*/false);

    runtime_source base;
    base.set("endpoints/output[0]/name", "a").set("endpoints/output[1]/name", "b");
    runtime_source over;
    over.set("endpoints/output[0]/name", "c");

    auto r = load_config(space, source_stack{std::move(base), std::move(over)}, {});
    REQUIRE(r.has_value());
    auto n = names(*r);
    // The override addresses output[0] alone, so the base's output[1] stays.
    REQUIRE(n == std::vector<std::string>{"c", "b"});
}

TEST_CASE("Unite — layers union; a duplicate identifier across layers is loud",
          "[keyed]")
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
    SECTION("a duplicate identifier WITHIN one layer is also a strict-additive error")
    {
        auto space = make_space(merge_mode::unite);
        runtime_source dup;
        dup.set("endpoints/output[0]/name", "a");
        dup.set("endpoints/output[1]/name", "a");  // same key, SAME layer

        auto r = load_config(space, source_stack{std::move(dup)}, {});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(mentions(r.error(), "strict-additive"));
        REQUIRE(mentions(r.error(), "'a'"));
    }
}

TEST_CASE("replace_by_key — a matching identifier replaces the whole element",
          "[keyed]")
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

TEST_CASE("A keyed mode without an identity group is a loud error", "[keyed]")
{
    auto space = make_space(merge_mode::unite, /*with_identity=*/false);
    runtime_source src;
    src.set("endpoints/output[0]/name", "a");
    auto r = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(mentions(r.error(), "no identity group"));
}

TEST_CASE("Keyed merge composes with strain slicing (nested in a pkey strain)",
          "[keyed]")
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
    auto space = nucleus::builder_result_test::built(std::move(b));

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

TEST_CASE("a CLI plain-ordinal path under a keyed-merge container is recognized "
          "as an override, not swallowed as a flat leaf", "[keyed]")
{
    auto space = make_space(merge_mode::unite);

    argv_source argv(std::vector<std::string>{"--endpoints-output-0-name=x"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = load_config(space, source_stack{std::move(argv)}, {});
    // The empty keyed-merge collection has zero existing instances, so this
    // deferred override legitimately fails as out of range -- the full positive
    // round trip (the override reaching a merged instance) is completed once
    // the deferred-override apply pass moves past slice(). This test proves
    // only that the path was RECOGNIZED as an ordinal override attempt, not
    // misgrouped into the keyed-composition divert as a flat leaf.
    REQUIRE_FALSE(loaded);
    REQUIRE_FALSE(mentions(loaded.error(), "identifier"));
    REQUIRE(mentions(loaded.error(), "out of range"));
}

TEST_CASE("flat multi-leaf entries under a keyed-merge container group per "
          "(rank, container) into one instance", "[keyed]")
{
    auto space = make_space(merge_mode::unite);

    runtime_source base;
    base.set("endpoints/output[0]/name", "a");

    runtime_source flat_override;
    flat_override.set("endpoints/output/name", "c")
                 .set("endpoints/output/addr", "10.0.0.1");

    auto r = load_config(space,
        source_stack{std::move(base), std::move(flat_override)}, {});
    REQUIRE(r.has_value());

    auto n = names(*r);
    std::sort(n.begin(), n.end());
    REQUIRE(n == std::vector<std::string>{"a", "c"});

    // "c" merges as a new instance carrying BOTH leaves, not fragmented into
    // two one-leaf instances -- survivors sort by (rank, ordinal), so "c"
    // (rank 1) lands after "a" (rank 0) at ordinal 1.
    REQUIRE(r->get("endpoints/output[1]/name") == "c");
    REQUIRE(r->get("endpoints/output[1]/addr") == "10.0.0.1");
}

TEST_CASE("flat multi-leaf entries under a keyed-merge container arriving "
          "field-major fail loudly instead of silently mis-pairing leaves",
          "[keyed]")
{
    auto space = make_space(merge_mode::unite);

    // Field-major order: both instances' "name" leaves arrive before either
    // instance's "addr" leaf. The grouping's instance-major contract cannot
    // disambiguate which "addr" belongs to which "name" from suffix repeats
    // alone, so this must fail loudly rather than mis-pair "10.0.0.2" onto
    // "x"'s instance (or fabricate a spurious third instance).
    runtime_source flat_field_major;
    flat_field_major.set("endpoints/output/name", "x")
                    .set("endpoints/output/name", "y")
                    .set("endpoints/output/addr", "10.0.0.1")
                    .set("endpoints/output/addr", "10.0.0.2");

    auto r = load_config(space, source_stack{std::move(flat_field_major)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == errc::layering_violation);
    REQUIRE(mentions(r.error(), "instance-major"));
    REQUIRE(mentions(r.error(), "endpoints/output"));
}

TEST_CASE("a flat source without duplicate_keys cannot address two keyed "
          "instances in one layer", "[keyed]")
{
    auto space = make_space(merge_mode::unite);

    // A capable base layer satisfies the schema's whole-stack nesting/
    // duplicate_keys requirement; the env layer's OWN (empty) descriptor still
    // governs its own entries' per-entry capability check inside the divert.
    runtime_source base;
    base.set("endpoints/output[0]/name", "seed");

    env_source flat({{"endpoints/output/name", "x"}, {"endpoints/output/name", "y"}});

    auto r = load_config(space, source_stack{std::move(base), std::move(flat)}, {});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == errc::layering_violation);
    REQUIRE(mentions(r.error(), "stack[1]"));
    REQUIRE(mentions(r.error(), "endpoints/output"));
}
