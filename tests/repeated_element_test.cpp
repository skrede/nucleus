// Repeated-values mode: schema flag, fold accumulation/replacement, get_all()
// accessor, relay through keyed containers, capability gating, provenance.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/capability.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/configuration_source/env/env_source.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using nucleus::anchor;

namespace {

std::unique_ptr<nucleus::configuration_source> xml_of(const std::string &text)
{
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from_string(text));
}

// Registers a flat schema: a <config> root container with a repeated <tag> leaf.
// All simple repeated-element tests use this schema and the path "config/tag".
void declare_tags_schema(nucleus::configuration_space &engine)
{
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(
        nucleus::repeated_element("tag", anchor::keyspace("config")));
}

// Registers: cluster/server keyed by name, plus a repeated leaf "tags".
void declare_cluster_tags(nucleus::configuration_space &engine)
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
struct dual_entry_source : nucleus::configuration_source
{
    std::string path;
    explicit dual_entry_source(std::string p) : path(std::move(p)) {}

    nucleus::configuration_source_result pull() override
    {
        nucleus::configuration_source_batch batch;
        nucleus::capability_descriptor no_caps{};
        batch.entries.push_back({path, nucleus::value::owned("v1"), no_caps});
        batch.entries.push_back({path, nucleus::value::owned("v2"), no_caps});
        return batch;
    }

    nucleus::capability_descriptor capabilities() const override
    {
        return {};
    }
};

}

TEST_CASE("N values in one layer -- order preserved", "[repeated][ordering]")
{
    nucleus::configuration_space engine;
    declare_tags_schema(engine);

    auto src = xml_of("<config><tag>a</tag><tag>b</tag><tag>c</tag></config>");
    nucleus::configuration_source_stack stack;
    stack.add(*src, std::size_t{10}, "doc");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get_all("config/tag") == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("cross-layer replace -- higher rank replaces lower collection wholesale",
          "[repeated][layering]")
{
    nucleus::configuration_space engine;
    declare_tags_schema(engine);

    auto src1 = xml_of("<config><tag>x</tag><tag>y</tag></config>");
    auto src2 = xml_of("<config><tag>p</tag></config>");

    nucleus::configuration_source_stack stack;
    stack.add(*src1, std::size_t{10}, "base");
    stack.add(*src2, std::size_t{20}, "overlay");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // The higher-rank layer's singleton replaces the lower-rank collection.
    REQUIRE(config.get_all("config/tag") == std::vector<std::string>{"p"});
}

TEST_CASE("get() on repeated path returns last value", "[repeated][accessor]")
{
    nucleus::configuration_space engine;
    declare_tags_schema(engine);

    auto src = xml_of("<config><tag>a</tag><tag>b</tag><tag>c</tag></config>");
    nucleus::configuration_source_stack stack;
    stack.add(*src, std::size_t{10}, "doc");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // get() on a repeated path returns the last element.
    REQUIRE(config.get("config/tag") == "c");
}

TEST_CASE("get_all() on single-value path returns one-element vector", "[repeated][accessor]")
{
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::element("key", anchor::keyspace("config")));

    auto src = xml_of("<config><key>v</key></config>");
    nucleus::configuration_source_stack stack;
    stack.add(*src, std::size_t{10}, "doc");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get_all("config/key") == std::vector<std::string>{"v"});
}

TEST_CASE("get_all() on absent path returns empty vector", "[repeated][accessor]")
{
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::element("key", anchor::keyspace("config")));

    auto src = xml_of("<config><key>v</key></config>");
    nucleus::configuration_source_stack stack;
    stack.add(*src, std::size_t{10}, "doc");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get_all("nonexistent") == std::vector<std::string>{});
}

TEST_CASE("keys() returns repeated path exactly once", "[repeated][accessor]")
{
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::element("other", anchor::keyspace("config")));
    engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("config")));

    auto src = xml_of("<config><other>x</other><tag>a</tag><tag>b</tag></config>");
    nucleus::configuration_source_stack stack;
    stack.add(*src, std::size_t{10}, "doc");

    auto loaded = engine.load_configuration(stack);
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
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("config", anchor::root()));

    auto el = nucleus::repeated_element("tag", anchor::keyspace("config"));
    el.required = true;
    engine.register_element(el);

    auto src = xml_of("<config><tag>present</tag></config>");
    nucleus::configuration_source_stack stack;
    stack.add(*src, std::size_t{10}, "doc");

    // One value satisfies the required check.
    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get_all("config/tag") == std::vector<std::string>{"present"});
}

TEST_CASE("repeated leaf under keyed container with selection resolves to collection",
          "[repeated][keyed][relay]")
{
    nucleus::configuration_space engine;
    declare_cluster_tags(engine);
    REQUIRE(engine.select("yin"));

    const char *doc = R"(
        <cluster>
            <server name="yin"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    auto src = xml_of(doc);
    nucleus::configuration_source_stack stack;
    stack.add(*src, std::size_t{10}, "doc");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get_all("cluster/server/tags")
            == std::vector<std::string>{"alpha", "beta"});
}

TEST_CASE("token expansion per value -- each value expanded independently",
          "[repeated][tokens]")
{
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::repeated_element("val", anchor::keyspace("config")));

    // ${string.upper(x)} is the built-in string tokenizer's upper function.
    // Two values use different arguments so they expand to distinct strings,
    // confirming each element is independently expanded before accumulation.
    auto src = xml_of(
        "<config>"
        "<val>${string.upper(alpha)}_1</val>"
        "<val>${string.upper(beta)}_2</val>"
        "</config>");
    nucleus::configuration_source_stack stack;
    stack.add(*src, std::size_t{10}, "doc");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    const std::vector<std::string> vals = config.get_all("config/val");
    REQUIRE(vals.size() == 2);
    REQUIRE(vals[0] == "ALPHA_1");
    REQUIRE(vals[1] == "BETA_2");
}

TEST_CASE("attach-time rejection of repeated + identity", "[repeated][attach][reject]")
{
    nucleus::configuration_space engine;

    // primary_key_element sets identity=true; adding repeated=true is illegal.
    auto el = nucleus::primary_key_element("id", anchor::root());
    el.repeated = true;

    auto result = engine.register_element(el);
    REQUIRE(!result);
    REQUIRE(result.error().find("primary key") != std::string::npos);
}

TEST_CASE("attach-time rejection of repeated + unique", "[repeated][attach][reject]")
{
    nucleus::configuration_space engine;
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
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("config", anchor::root()));
    engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("config")));

    auto fake = std::make_unique<dual_entry_source>("config/tag");
    nucleus::configuration_source_stack stack;
    stack.add(*fake, std::size_t{10}, "fake");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(!loaded);
    REQUIRE(loaded.error().find("duplicate_keys") != std::string::npos);
}

TEST_CASE("ASan: freeze copies values out before buffer drop", "[repeated][lifetime]")
{
    // Resolve inside a lambda scope so the configuration_space and its source
    // buffers are destroyed before we read from the returned configuration.
    nucleus::configuration cfg = [&]() {
        nucleus::configuration_space engine;
        engine.register_element(nucleus::element("config", anchor::root()));
        engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("config")));

        auto src = xml_of("<config><tag>x</tag><tag>y</tag></config>");
        nucleus::configuration_source_stack stack;
        stack.add(*src, std::size_t{10}, "doc");

        auto result = engine.load_configuration(stack);
        REQUIRE(result);
        return std::move(result).value();
    }(); // engine, src, and all buffers destroyed here

    // Any use-after-free would trip under ASan.
    REQUIRE(cfg.get_all("config/tag") == std::vector<std::string>{"x", "y"});
}

TEST_CASE("relay_strain: higher-rank keyed collection wins over lower-rank flat scalar",
          "[repeated][keyed][relay][rank]")
{
    // Rank-inversion regression: a flat scalar at the unified path (rank 5) must
    // not silence a higher-ranked keyed collection (rank 10) during relay_strain.
    // Expected: collection wins; get_all returns {"alpha","beta"} and the scalar
    // is replaced.
    nucleus::configuration_space engine;
    declare_cluster_tags(engine);
    REQUIRE(engine.select("yin"));

    nucleus::env_source flat;
    flat.set("cluster/server/tags", "low");

    const char *doc = R"(
        <cluster>
            <server name="yin"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    auto src = xml_of(doc);
    nucleus::configuration_source_stack stack;
    stack.add(flat, std::size_t{5},  "flat-low");
    stack.add(*src, std::size_t{10}, "xml-high");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Collection at rank 10 wins over flat scalar at rank 5.
    REQUIRE(config.get_all("cluster/server/tags")
            == std::vector<std::string>{"alpha", "beta"});
    REQUIRE_FALSE(config.get("cluster/server/tags") == "low");
}

TEST_CASE("relay_strain: higher-rank flat override wins over lower-rank keyed collection",
          "[repeated][keyed][relay][rank]")
{
    // Argv-style override: a flat source at rank 20 must displace a lower-ranked
    // keyed collection (rank 10) during relay_strain. The flat source feeds the
    // unified repeated path directly, producing a single-element collection that
    // should survive because its rank is higher than the keyed collection.
    nucleus::configuration_space engine;
    declare_cluster_tags(engine);
    REQUIRE(engine.select("yin"));

    const char *doc = R"(
        <cluster>
            <server name="yin"><tags>alpha</tags><tags>beta</tags></server>
        </cluster>)";

    nucleus::env_source flat;
    flat.set("cluster/server/tags", "high");

    auto src = xml_of(doc);
    nucleus::configuration_source_stack stack;
    stack.add(*src,  std::size_t{10}, "xml-low");
    stack.add(flat,  std::size_t{20}, "flat-high");

    auto loaded = engine.load_configuration(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Flat source at rank 20 wins; only "high" remains.
    REQUIRE(config.get_all("cluster/server/tags") == std::vector<std::string>{"high"});
}

TEST_CASE("collection scope-policy exclusion and admission",
          "[repeated][keyed][scope_policy]")
{
    // Schema: cluster/server keyed by name, with a repeated leaf "tags".
    // Two-layer env_source stack (ranks below the document band so the
    // cross-layer re-open guard does not apply):
    //   L0 (rank 10): introduces "yin" (name + tags="base") -- Ld = 10.
    //   Lderived (rank 20): re-sets tags="derived" for "yin"; because the fold
    //     treats this as a new layer, replace_collection fires and the collection
    //     becomes {"derived"} with provenance rank 20.
    //
    // Under space_open_container_closed (excluded when rank > Ld=10):
    //   The relay filter sees rank 20 > Ld=10 and excludes the collection;
    //   cluster/server/tags is absent from the resolved configuration.
    //
    // Under container_open_until_next_strain (admitted below Ls):
    //   No competing strain above Ld, so Ls = max. Rank 20 < max, so the
    //   collection is admitted and relayed to cluster/server/tags = {"derived"}.

    SECTION("space_open_container_closed excludes collection above Ld")
    {
        nucleus::configuration_space engine;
        declare_cluster_tags(engine);
        REQUIRE(engine.select("yin"));
        // Default policy is space_open_container_closed.

        nucleus::env_source L0;
        L0.set("cluster/server/yin/name", "yin")
          .set("cluster/server/yin/tags", "base");

        nucleus::env_source Lderived;
        Lderived.set("cluster/server/yin/tags", "derived");

        nucleus::configuration_source_stack stack;
        stack.add(L0,       std::size_t{10}, "L0");
        stack.add(Lderived, std::size_t{20}, "Lderived");

        auto loaded = engine.load_configuration(stack);
        REQUIRE(loaded);
        const nucleus::configuration &config = loaded.value();

        // The rank-20 collection is excluded by the relay filter (20 > Ld=10).
        REQUIRE_FALSE(config.contains("cluster/server/tags"));
    }

    SECTION("container_open_until_next_strain admits collection up to Ls")
    {
        nucleus::configuration_space engine;
        declare_cluster_tags(engine);
        REQUIRE(engine.select("yin"));
        REQUIRE(engine.set_strain_scope(
            nucleus::strain_scope_policy::container_open_until_next_strain));

        nucleus::env_source L0;
        L0.set("cluster/server/yin/name", "yin")
          .set("cluster/server/yin/tags", "base");

        nucleus::env_source Lderived;
        Lderived.set("cluster/server/yin/tags", "derived");

        nucleus::configuration_source_stack stack;
        stack.add(L0,       std::size_t{10}, "L0");
        stack.add(Lderived, std::size_t{20}, "Lderived");

        auto loaded = engine.load_configuration(stack);
        REQUIRE(loaded);
        const nucleus::configuration &config = loaded.value();

        // No competing strain above Ld; Ls = max. Rank 20 < max; collection
        // admitted and relayed to the unified path.
        REQUIRE(config.get_all("cluster/server/tags")
                == std::vector<std::string>{"derived"});
    }
}
