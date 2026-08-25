// Repeated-values mode: schema flag, fold accumulation/replacement, get_all()
// accessor, relay through keyed containers, capability gating, provenance.

#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config.h"

#include "nucleus/capability.h"

#include "nucleus/config_source/config_source.h"

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
void declare_tags_schema(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("tag", anchor::keyspace("config"))));
}

// Registers: cluster/server keyed by name, plus a repeated leaf "tags".
void declare_cluster_tags(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("tags", anchor::keyspace("cluster/server"))));
}

// A minimal source that emits two entries for the same repeated path with
// no duplicate_keys capability -- used to verify the capability gate fires.
struct dual_entry_source
{
    std::string path;
    explicit dual_entry_source(std::string p) : path(std::move(p)) {}

    nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        nucleus::capability_descriptor no_caps{};
        batch.entries.push_back({path, nucleus::value::owned("v1"), no_caps});
        batch.entries.push_back({path, nucleus::value::owned("v2"), no_caps});
        return batch;
    }

    nucleus::capability_descriptor capabilities() const
    {
        return {};
    }
};

}

TEST_CASE("N values in one layer -- order preserved", "[repeated][ordering]")
{
    nucleus::config_space_builder engine;
    declare_tags_schema(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto src = xml_of("<config><tag>a</tag><tag>b</tag><tag>c</tag></config>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get_all("config/tag") == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("cross-layer replace -- higher rank replaces lower collection wholesale",
          "[repeated][layering]")
{
    nucleus::config_space_builder engine;
    declare_tags_schema(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto src1 = xml_of("<config><tag>x</tag><tag>y</tag></config>");
    auto src2 = xml_of("<config><tag>p</tag></config>");

    // src1 at lower precedence (stack[0]), src2 at higher precedence (stack[1]).
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src1), std::move(src2)},
        {});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // The higher-rank layer's singleton replaces the lower-rank collection.
    REQUIRE(config.get_all("config/tag") == std::vector<std::string>{"p"});
}

TEST_CASE("get() on indexed repeated path returns that element", "[repeated][accessor]")
{
    nucleus::config_space_builder engine;
    declare_tags_schema(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto src = xml_of("<config><tag>a</tag><tag>b</tag><tag>c</tag></config>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // Repeated values are stored as indexed scalars; get() by indexed key.
    REQUIRE(config.get("config/tag[0]") == "a");
    REQUIRE(config.get("config/tag[1]") == "b");
    REQUIRE(config.get("config/tag[2]") == "c");
    // Plain (unindexed) repeated path is absent in the new indexed-scalar model.
    REQUIRE_FALSE(config.get("config/tag").has_value());
}

TEST_CASE("get_all() on single-value path returns one-element vector", "[repeated][accessor]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("key", anchor::keyspace("config"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto src = xml_of("<config><key>v</key></config>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get_all("config/key") == std::vector<std::string>{"v"});
}

TEST_CASE("get_all() on absent path returns empty vector", "[repeated][accessor]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("key", anchor::keyspace("config"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto src = xml_of("<config><key>v</key></config>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get_all("nonexistent") == std::vector<std::string>{});
}

TEST_CASE("keys() returns one entry per indexed scalar instance", "[repeated][accessor]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("other", anchor::keyspace("config"))));
    REQUIRE(engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("config"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto src = xml_of("<config><other>x</other><tag>a</tag><tag>b</tag></config>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // In the indexed-scalar model, each repeated instance is its own key.
    // 2 tag instances + 1 other = 3 keys total.
    const std::vector<std::string> k = config.keys();
    REQUIRE(k.size() == 3);

    const bool has_other  = std::find(k.begin(), k.end(), "config/other")   != k.end();
    const bool has_tag0   = std::find(k.begin(), k.end(), "config/tag[0]")  != k.end();
    const bool has_tag1   = std::find(k.begin(), k.end(), "config/tag[1]")  != k.end();
    REQUIRE(has_other);
    REQUIRE(has_tag0);
    REQUIRE(has_tag1);
}

TEST_CASE("repeated path with required flag satisfies required check", "[repeated][required]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));

    auto el = nucleus::repeated_element("tag", anchor::keyspace("config"));
    el.required = true;
    REQUIRE(engine.register_element(el));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto src = xml_of("<config><tag>present</tag></config>");

    // One value satisfies the required check.
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get_all("config/tag") == std::vector<std::string>{"present"});
}

TEST_CASE("repeated leaf under keyed container with selection resolves to collection",
          "[repeated][keyed][relay]")
{
    nucleus::config_space_builder engine;
    declare_cluster_tags(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    const char *doc = R"(
        <cluster>
            <server name="primary"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    auto src = xml_of(doc);
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        nucleus::load_options{.selection = "primary"});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get_all("cluster/server/tags")
            == std::vector<std::string>{"alpha", "beta"});
}

TEST_CASE("token expansion per value -- each value expanded independently",
          "[repeated][tokens]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));
    REQUIRE(engine.register_element(nucleus::repeated_element("val", anchor::keyspace("config"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    // ${string.upper(value=x)} is the built-in string tokenizer's upper function.
    auto src = xml_of(
        "<config>"
        "<val>${string.upper(value=alpha)}_1</val>"
        "<val>${string.upper(value=beta)}_2</val>"
        "</config>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    const std::vector<std::string> vals = config.get_all("config/val");
    REQUIRE(vals.size() == 2);
    REQUIRE(vals[0] == "ALPHA_1");
    REQUIRE(vals[1] == "BETA_2");
}

TEST_CASE("attach-time rejection of repeated + identity", "[repeated][attach][reject]")
{
    nucleus::config_space_builder engine;

    // primary_key_element sets identity=true; adding repeated=true is illegal.
    auto el = nucleus::primary_key_element("id", anchor::root());
    el.repeated = true;

    auto result = engine.register_element(el);
    REQUIRE(!result);
    REQUIRE(result.error().message.find("primary key") != std::string::npos);
}

TEST_CASE("attach-time rejection of repeated + unique", "[repeated][attach][reject]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("container", anchor::root())));

    // unique_element sets unique=true; adding repeated=true is illegal.
    auto el = nucleus::unique_element("val", anchor::keyspace("container"));
    el.repeated = true;

    auto result = engine.register_element(el);
    REQUIRE(!result);
    REQUIRE(result.error().message.find("unique") != std::string::npos);
}

TEST_CASE("capability degradation -- non-duplicate_keys source into repeated field fails",
          "[repeated][capability]")
{
    // A flat repeated leaf isolates the duplicate_keys gate: the only structural
    // capability the schema needs is duplicate_keys, so a source lacking it fails
    // naming that capability specifically (not nesting).
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::repeated_element("tag", anchor::root())));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    dual_entry_source fake("tag");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(fake)},
        {});
    REQUIRE(!loaded);
    REQUIRE(loaded.error().message.find("duplicate_keys") != std::string::npos);
}

TEST_CASE("ASan: freeze copies values out before buffer drop", "[repeated][lifetime]")
{
    // Resolve inside a lambda scope so the space and its source buffers are
    // destroyed before we read from the returned config.
    nucleus::config cfg = [&]() {
        nucleus::config_space_builder engine;
        REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));
        REQUIRE(engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("config"))));
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        auto src = xml_of("<config><tag>x</tag><tag>y</tag></config>");
        auto result = nucleus::load_config(space,
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
    nucleus::config_space_builder engine;
    declare_cluster_tags(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    nucleus::env_source flat;
    flat.set("cluster/server/tags", "low");

    const char *doc = R"(
        <cluster>
            <server name="primary"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    auto src = xml_of(doc);
    // flat at lower precedence (stack[0]), xml at higher precedence (stack[1]).
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(flat), std::move(src)},
        nucleus::load_options{.selection = "primary"});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // Collection at higher rank wins over flat scalar at lower rank.
    REQUIRE(config.get_all("cluster/server/tags")
            == std::vector<std::string>{"alpha", "beta"});
    REQUIRE_FALSE(config.get("cluster/server/tags") == "low");
}

TEST_CASE("relay_strain: higher-rank flat override wins over lower-rank keyed collection",
          "[repeated][keyed][relay][rank]")
{
    nucleus::config_space_builder engine;
    declare_cluster_tags(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    const char *doc = R"(
        <cluster>
            <server name="primary"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    nucleus::env_source flat;
    flat.set("cluster/server/tags", "high");

    auto src = xml_of(doc);
    // xml at lower precedence (stack[0]), flat at higher precedence (stack[1]).
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src), std::move(flat)},
        nucleus::load_options{.selection = "primary"});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // Flat source at higher rank wins; only "high" remains.
    REQUIRE(config.get_all("cluster/server/tags") == std::vector<std::string>{"high"});
}

TEST_CASE("collection scope-policy exclusion and admission",
          "[repeated][keyed][scope_policy]")
{
    SECTION("space_open_container_closed excludes collection above Ld")
    {
        nucleus::config_space_builder engine;
        declare_cluster_tags(engine);
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        nucleus::runtime_source L0;
        L0.set("cluster/server/primary/name", "primary")
          .set("cluster/server/primary/tags", "base");

        nucleus::runtime_source Lderived;
        Lderived.set("cluster/server/primary/tags", "derived");

        // L0 at lower precedence (stack[0]), Lderived at higher precedence (stack[1]).
        // Default policy is space_open_container_closed.
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{std::move(L0), std::move(Lderived)},
            nucleus::load_options{.selection = "primary"});
        REQUIRE(loaded);
        const nucleus::config &config = loaded.value();

        // The rank-20 collection is excluded by the relay filter (20 > Ld=10).
        REQUIRE_FALSE(config.contains("cluster/server/tags"));
    }

    SECTION("container_open_until_next_strain admits collection up to Ls")
    {
        nucleus::config_space_builder engine;
        declare_cluster_tags(engine);
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        nucleus::runtime_source L0;
        L0.set("cluster/server/primary/name", "primary")
          .set("cluster/server/primary/tags", "base");

        nucleus::runtime_source Lderived;
        Lderived.set("cluster/server/primary/tags", "derived");

        // L0 at lower precedence (stack[0]), Lderived at higher precedence (stack[1]).
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{std::move(L0), std::move(Lderived)},
            nucleus::load_options{
                .selection = "primary",
                .scope = nucleus::strain_scope_policy::container_open_until_next_strain});
        REQUIRE(loaded);
        const nucleus::config &config = loaded.value();

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
    nucleus::config_space_builder engine;
    declare_tags_schema(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    nucleus::runtime_source src;
    src.set("config/tag", "alpha").set("config/tag", "beta");

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get_all("config/tag")
            == std::vector<std::string>{"alpha", "beta"});
}
