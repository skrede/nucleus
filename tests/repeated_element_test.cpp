// Repeated-values mode: schema flag, fold accumulation/replacement, get_all()
// accessor, relay through keyed containers, capability gating, provenance.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/configuration.h"

#include "nucleus/capability.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/env/env_source.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <algorithm>

using nucleus::anchor;

namespace {

nucleus::xml_source xml_of(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

// Registers a flat schema: a <config> root container with a repeated <tag> leaf.
void declare_tags_schema(nucleus::configuration_space_builder &engine)
{
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(
        nucleus::repeated_element("tag", anchor::keyspace("config")));
}

// Registers: cluster/server keyed by name, plus a repeated leaf "tags".
void declare_cluster_tags(nucleus::configuration_space_builder &engine)
{
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::repeated_element("tags", anchor::keyspace("cluster/server")));
}

// A minimal source that emits two entries for the same repeated path with
// no duplicate_keys capability -- used to verify the capability gate fires.
struct dual_entry_source
{
    std::string path;
    explicit dual_entry_source(std::string p) : path(std::move(p)) {}

    [[nodiscard]] nucleus::configuration_source_result pull()
    {
        nucleus::configuration_source_batch batch;
        nucleus::capability_descriptor no_caps{};
        batch.entries.push_back({path, nucleus::value::owned("v1"), no_caps});
        batch.entries.push_back({path, nucleus::value::owned("v2"), no_caps});
        return batch;
    }

    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {};
    }
};

}

TEST_CASE("N values in one layer -- order preserved", "[repeated][ordering]")
{
    nucleus::configuration_space_builder engine;
    declare_tags_schema(engine);
    nucleus::configuration_space space = engine.build();

    auto src = xml_of("<config><tag>a</tag><tag>b</tag><tag>c</tag></config>");
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get_all("config/tag") == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("cross-layer replace -- higher rank replaces lower collection wholesale",
          "[repeated][layering]")
{
    nucleus::configuration_space_builder engine;
    declare_tags_schema(engine);
    nucleus::configuration_space space = engine.build();

    auto src1 = xml_of("<config><tag>x</tag><tag>y</tag></config>");
    auto src2 = xml_of("<config><tag>p</tag></config>");

    // src1 at lower precedence (stack[0]), src2 at higher precedence (stack[1]).
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src1), std::move(src2)},
        {});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // The higher-rank layer's singleton replaces the lower-rank collection.
    REQUIRE(config.get_all("config/tag") == std::vector<std::string>{"p"});
}

TEST_CASE("get() on repeated path returns last value", "[repeated][accessor]")
{
    nucleus::configuration_space_builder engine;
    declare_tags_schema(engine);
    nucleus::configuration_space space = engine.build();

    auto src = xml_of("<config><tag>a</tag><tag>b</tag><tag>c</tag></config>");
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // get() on a repeated path returns the last element.
    REQUIRE(config.get("config/tag") == "c");
}

TEST_CASE("get_all() on single-value path returns one-element vector", "[repeated][accessor]")
{
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::element("key", anchor::keyspace("config")));
    nucleus::configuration_space space = engine.build();

    auto src = xml_of("<config><key>v</key></config>");
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get_all("config/key") == std::vector<std::string>{"v"});
}

TEST_CASE("get_all() on absent path returns empty vector", "[repeated][accessor]")
{
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::element("key", anchor::keyspace("config")));
    nucleus::configuration_space space = engine.build();

    auto src = xml_of("<config><key>v</key></config>");
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get_all("nonexistent") == std::vector<std::string>{});
}

TEST_CASE("keys() returns repeated path exactly once", "[repeated][accessor]")
{
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::element("other", anchor::keyspace("config")));
    engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("config")));
    nucleus::configuration_space space = engine.build();

    auto src = xml_of("<config><other>x</other><tag>a</tag><tag>b</tag></config>");
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    const std::vector<std::string> k = config.keys();
    REQUIRE(k.size() == 2);

    const bool has_other = std::find(k.begin(), k.end(), "config/other") != k.end();
    const bool has_tag   = std::find(k.begin(), k.end(), "config/tag")   != k.end();
    REQUIRE(has_other);
    REQUIRE(has_tag);
}

TEST_CASE("repeated path with required flag satisfies required check", "[repeated][required]")
{
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("config", anchor::root()));

    auto el = nucleus::repeated_element("tag", anchor::keyspace("config"));
    el.required = true;
    engine.register_element(el);
    nucleus::configuration_space space = engine.build();

    auto src = xml_of("<config><tag>present</tag></config>");

    // One value satisfies the required check.
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get_all("config/tag") == std::vector<std::string>{"present"});
}

TEST_CASE("repeated leaf under keyed container with selection resolves to collection",
          "[repeated][keyed][relay]")
{
    nucleus::configuration_space_builder engine;
    declare_cluster_tags(engine);
    nucleus::configuration_space space = engine.build();

    const char *doc = R"(
        <cluster>
            <server name="primary"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    auto src = xml_of(doc);
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        nucleus::load_options{.selection = "primary"});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get_all("cluster/server/tags")
            == std::vector<std::string>{"alpha", "beta"});
}

TEST_CASE("token expansion per value -- each value expanded independently",
          "[repeated][tokens]")
{
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::repeated_element("val", anchor::keyspace("config")));
    nucleus::configuration_space space = engine.build();

    // ${string.upper(x)} is the built-in string tokenizer's upper function.
    auto src = xml_of(
        "<config>"
        "<val>${string.upper(alpha)}_1</val>"
        "<val>${string.upper(beta)}_2</val>"
        "</config>");
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    const std::vector<std::string> vals = config.get_all("config/val");
    REQUIRE(vals.size() == 2);
    REQUIRE(vals[0] == "ALPHA_1");
    REQUIRE(vals[1] == "BETA_2");
}

TEST_CASE("attach-time rejection of repeated + identity", "[repeated][attach][reject]")
{
    nucleus::configuration_space_builder engine;

    // primary_key_element sets identity=true; adding repeated=true is illegal.
    auto el = nucleus::primary_key_element("id", anchor::root());
    el.repeated = true;

    auto result = engine.register_element(el);
    REQUIRE(!result);
    REQUIRE(result.error().find("primary key") != std::string::npos);
}

TEST_CASE("attach-time rejection of repeated + unique", "[repeated][attach][reject]")
{
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("container", anchor::root()));

    // unique_element sets unique=true; adding repeated=true is illegal.
    auto el = nucleus::unique_element("val", anchor::keyspace("container"));
    el.repeated = true;

    auto result = engine.register_element(el);
    REQUIRE(!result);
    REQUIRE(result.error().find("unique") != std::string::npos);
}

TEST_CASE("capability degradation -- non-duplicate_keys source into repeated field fails",
          "[repeated][capability]")
{
    // A flat repeated leaf isolates the duplicate_keys gate: the only structural
    // capability the schema needs is duplicate_keys, so a source lacking it fails
    // naming that capability specifically (not nesting).
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::repeated_element("tag", anchor::root()));
    nucleus::configuration_space space = engine.build();

    dual_entry_source fake("tag");
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(fake)},
        {});
    REQUIRE(!loaded);
    REQUIRE(loaded.error().find("duplicate_keys") != std::string::npos);
}

TEST_CASE("ASan: freeze copies values out before buffer drop", "[repeated][lifetime]")
{
    // Resolve inside a lambda scope so the space and its source buffers are
    // destroyed before we read from the returned configuration.
    nucleus::configuration cfg = [&]() {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("config", anchor::root()));
        engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("config")));
        nucleus::configuration_space space = engine.build();

        auto src = xml_of("<config><tag>x</tag><tag>y</tag></config>");
        auto result = nucleus::load(space,
            nucleus::source_stack{std::move(src)},
            {});
        REQUIRE(result);
        return std::move(result).value();
    }(); // space, src, and all buffers destroyed here

    // Any use-after-free would trip under ASan.
    REQUIRE(cfg.get_all("config/tag") == std::vector<std::string>{"x", "y"});
}

TEST_CASE("relay_strain: higher-rank keyed collection wins over lower-rank flat scalar",
          "[repeated][keyed][relay][rank]")
{
    nucleus::configuration_space_builder engine;
    declare_cluster_tags(engine);
    nucleus::configuration_space space = engine.build();

    nucleus::env_source flat;
    flat.set("cluster/server/tags", "low");

    const char *doc = R"(
        <cluster>
            <server name="primary"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    auto src = xml_of(doc);
    // flat at lower precedence (stack[0]), xml at higher precedence (stack[1]).
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(flat), std::move(src)},
        nucleus::load_options{.selection = "primary"});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Collection at higher rank wins over flat scalar at lower rank.
    REQUIRE(config.get_all("cluster/server/tags")
            == std::vector<std::string>{"alpha", "beta"});
    REQUIRE_FALSE(config.get("cluster/server/tags") == "low");
}

TEST_CASE("relay_strain: higher-rank flat override wins over lower-rank keyed collection",
          "[repeated][keyed][relay][rank]")
{
    nucleus::configuration_space_builder engine;
    declare_cluster_tags(engine);
    nucleus::configuration_space space = engine.build();

    const char *doc = R"(
        <cluster>
            <server name="primary"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    nucleus::env_source flat;
    flat.set("cluster/server/tags", "high");

    auto src = xml_of(doc);
    // xml at lower precedence (stack[0]), flat at higher precedence (stack[1]).
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src), std::move(flat)},
        nucleus::load_options{.selection = "primary"});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Flat source at higher rank wins; only "high" remains.
    REQUIRE(config.get_all("cluster/server/tags") == std::vector<std::string>{"high"});
}

TEST_CASE("collection scope-policy exclusion and admission",
          "[repeated][keyed][scope_policy]")
{
    SECTION("space_open_container_closed excludes collection above Ld")
    {
        nucleus::configuration_space_builder engine;
        declare_cluster_tags(engine);
        nucleus::configuration_space space = engine.build();

        nucleus::runtime_source L0;
        L0.set("cluster/server/primary/name", "primary")
          .set("cluster/server/primary/tags", "base");

        nucleus::runtime_source Lderived;
        Lderived.set("cluster/server/primary/tags", "derived");

        // L0 at lower precedence (stack[0]), Lderived at higher precedence (stack[1]).
        // Default policy is space_open_container_closed.
        auto loaded = nucleus::load(space,
            nucleus::source_stack{std::move(L0), std::move(Lderived)},
            nucleus::load_options{.selection = "primary"});
        REQUIRE(loaded);
        const nucleus::configuration &config = loaded.value();

        // The rank-20 collection is excluded by the relay filter (20 > Ld=10).
        REQUIRE_FALSE(config.contains("cluster/server/tags"));
    }

    SECTION("container_open_until_next_strain admits collection up to Ls")
    {
        nucleus::configuration_space_builder engine;
        declare_cluster_tags(engine);
        nucleus::configuration_space space = engine.build();

        nucleus::runtime_source L0;
        L0.set("cluster/server/primary/name", "primary")
          .set("cluster/server/primary/tags", "base");

        nucleus::runtime_source Lderived;
        Lderived.set("cluster/server/primary/tags", "derived");

        // L0 at lower precedence (stack[0]), Lderived at higher precedence (stack[1]).
        auto loaded = nucleus::load(space,
            nucleus::source_stack{std::move(L0), std::move(Lderived)},
            nucleus::load_options{
                .selection = "primary",
                .scope = nucleus::strain_scope_policy::container_open_until_next_strain});
        REQUIRE(loaded);
        const nucleus::configuration &config = loaded.value();

        // No competing strain above Ld; Ls = max. Rank 20 < max; collection
        // admitted and relayed to the unified path.
        REQUIRE(config.get_all("cluster/server/tags")
                == std::vector<std::string>{"derived"});
    }
}

TEST_CASE("a runtime source supplies multiple values for a repeated path",
          "[repeated][runtime]")
{
    // The runtime source declares duplicate_keys at BOTH the source and the entry
    // level, so the auto-gate's admit decision and the fold's per-entry duplicate
    // check agree: two .set() calls on a repeated path compose into a collection
    // instead of failing as a flat-source violation.
    nucleus::configuration_space_builder engine;
    declare_tags_schema(engine);
    nucleus::configuration_space space = engine.build();

    nucleus::runtime_source src;
    src.set("config/tag", "alpha").set("config/tag", "beta");

    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get_all("config/tag")
            == std::vector<std::string>{"alpha", "beta"});
}
