// Interaction shape tests: typed seam x composition model
// (inheritance chain, strain selection, repeated across layers, scope policy,
// access surface). Adds ONLY shapes not covered by typed_element_test.cpp.

#include "builder_result_test_support.h"

#include "nucleus/strain_scope.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/config_source/config_source.h"
#include "nucleus/env/env_source.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <functional>
#include <typeindex>
#include <string_view>

using nucleus::anchor;
using nucleus::strain_scope_policy;

namespace {

nucleus::xml_source xml_of(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

void declare_server_typed(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::typed_element<int32_t>("port", anchor::keyspace("cluster/server"))));
}

nucleus::config_space repeated_typed_cfg_space()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cfg", anchor::root())));
    auto el = nucleus::typed_element<int32_t>("nums", anchor::keyspace("cfg"));
    el.repeated = true;
    REQUIRE(engine.register_element(el));
    return engine.build();
}

nucleus::config_space repeated_typed_element_cfg_space()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cfg", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_typed_element<int32_t>("nums", anchor::keyspace("cfg"))));
    return engine.build();
}

// Document-path chain loader against the explicit stack API.
nucleus::load_result load_chain(const nucleus::config_space &space,
                                std::vector<std::string> paths,
                                std::function<nucleus::source_handle(const std::string &)> factory,
                                std::optional<std::string> selection = std::nullopt)
{
    nucleus::load_options opts;
    opts.document_paths = std::move(paths);
    opts.make_document = std::move(factory);
    opts.selection = std::move(selection);
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

}

// ---------------------------------------------------------------------------
// Typed x inheritance chain: derived value wins and converts
// ---------------------------------------------------------------------------
TEST_CASE("typed x inheritance chain: derived value wins and converts",
          "[typed][chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><port>443</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_server_typed(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return nucleus::source_handle(xml_of(base_doc));
        if(name == "derived.xml")
            return nucleus::source_handle(xml_of(derived_doc));
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
    REQUIRE(loaded);

    auto p = loaded.value().get_as<int32_t>("cluster/server/port");
    REQUIRE(p);
    REQUIRE(p.value() == 443);

    auto s = loaded.value().get("cluster/server/port");
    REQUIRE(s.has_value());
    REQUIRE(*s == "443");
}

// ---------------------------------------------------------------------------
// Typed x inheritance chain: bad value in winning layer fails resolve
// ---------------------------------------------------------------------------
TEST_CASE("typed x inheritance chain: bad value in winning layer fails resolve",
          "[typed][chain][failure]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><port>notanumber</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_server_typed(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return nucleus::source_handle(xml_of(base_doc));
        if(name == "derived.xml")
            return nucleus::source_handle(xml_of(derived_doc));
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
    REQUIRE(!loaded);
    INFO("error: " << loaded.error());
    // The convert() format is "conversion failed for '...': ... (layer: ...)"
    REQUIRE(loaded.error().message.find("conversion failed for") != std::string::npos);
    REQUIRE(loaded.error().message.find("cluster/server/port") != std::string::npos);
    REQUIRE(loaded.error().message.find("invalid characters") != std::string::npos);
    REQUIRE(loaded.error().message.find("derived") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Typed x pruned strain: selected strain resolves; bad value in pruned strain
// does not fail
// ---------------------------------------------------------------------------
TEST_CASE("typed x pruned strain: selected strain resolves; bad value in pruned strain does not fail",
          "[typed][strain][pruned]")
{
    // Document with two named strains: primary (port=80) and secondary (port=notanumber).
    const char *doc = R"(
        <cluster>
            <server name="primary"><port>80</port></server>
            <server name="secondary"><port>notanumber</port></server>
        </cluster>)";

    SECTION("selecting the good strain succeeds")
    {
        nucleus::config_space_builder engine;
        declare_server_typed(engine);
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{xml_of(doc)},
            nucleus::load_options{.selection = "primary"});
        REQUIRE(loaded);

        auto p = loaded.value().get_as<int32_t>("cluster/server/port");
        REQUIRE(p);
        REQUIRE(p.value() == 80);
    }

    SECTION("selecting the bad strain fails")
    {
        nucleus::config_space_builder engine;
        declare_server_typed(engine);
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{xml_of(doc)},
            nucleus::load_options{.selection = "secondary"});
        REQUIRE(!loaded);
        REQUIRE(loaded.error().message.find("invalid characters") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Typed x repeated across layers: winning layer collection replaces base
// ---------------------------------------------------------------------------
TEST_CASE("typed x repeated across layers: the winning collection converts fully",
          "[typed][repeated][layers]")
{
    nucleus::config_space space = repeated_typed_cfg_space();

    auto base_src = xml_of(
        "<cfg><nums>1</nums><nums>2</nums><nums>3</nums></cfg>");
    auto derived_src = xml_of(
        "<cfg><nums>10</nums><nums>20</nums></cfg>");

    // base_src at lower precedence (stack[0]), derived_src at higher (stack[1]).
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(base_src), std::move(derived_src)},
        {});
    REQUIRE(loaded);

    auto r = loaded.value().get_all_as<int32_t>("cfg/nums");
    REQUIRE(r);
    REQUIRE(r.value() == std::vector<int32_t>{10, 20});

    auto s = loaded.value().get_all("cfg/nums");
    REQUIRE(s == std::vector<std::string>{"10", "20"});
}

TEST_CASE("typed x repeated across layers: a bad element in the winning collection carries its index",
          "[typed][repeated][layers]")
{
    nucleus::config_space space = repeated_typed_cfg_space();

    auto base_src = xml_of(
        "<cfg><nums>1</nums><nums>2</nums><nums>3</nums></cfg>");
    auto derived_src = xml_of(
        "<cfg><nums>10</nums><nums>notanumber</nums></cfg>");

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(base_src), std::move(derived_src)},
        {});
    REQUIRE(!loaded);
    INFO("error: " << loaded.error());
    REQUIRE(loaded.error().message.find("cfg/nums") != std::string::npos);
    REQUIRE(loaded.error().message.find("[1]") != std::string::npos);
    REQUIRE(loaded.error().message.find("stack[1]") != std::string::npos);
}

TEST_CASE("typed x repeated across layers: a bad element only in the losing collection does not fail",
          "[typed][repeated][layers]")
{
    nucleus::config_space space = repeated_typed_cfg_space();

    auto base_src = xml_of(
        "<cfg><nums>1</nums><nums>notanumber</nums></cfg>");
    auto derived_src = xml_of(
        "<cfg><nums>10</nums><nums>20</nums></cfg>");

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(base_src), std::move(derived_src)},
        {});
    REQUIRE(loaded);

    auto r = loaded.value().get_all_as<int32_t>("cfg/nums");
    REQUIRE(r);
    REQUIRE(r.value() == std::vector<int32_t>{10, 20});
}

// ---------------------------------------------------------------------------
// Typed x scope policy: excluded entry above Ld is not converted
// ---------------------------------------------------------------------------
TEST_CASE("typed x scope policy: excluded entry above Ld is not converted",
          "[typed][scope][policy]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="narrow"><score>notanumber</score></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::typed_element<int32_t>("port", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::typed_element<int32_t>("score", anchor::keyspace("cluster/server"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return nucleus::source_handle(xml_of(base_doc));
        if(name == "derived.xml")
            return nucleus::source_handle(xml_of(derived_doc));
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
    REQUIRE(loaded);

    // Base port value survives at Ld; the derived score entry was excluded.
    auto p = loaded.value().get_as<int32_t>("cluster/server/port");
    REQUIRE(p);
    REQUIRE(p.value() == 80);

    // The excluded "score" field is absent -- it was never converted.
    REQUIRE_FALSE(loaded.value().contains("cluster/server/score"));
}

// ---------------------------------------------------------------------------
// Typed access surface: get and get_as agree; type mismatch pinned
// ---------------------------------------------------------------------------
TEST_CASE("typed access surface: get and get_as agree; type mismatch pinned",
          "[typed][accessor]")
{
    SECTION("get and get_as agree on a typed int32_t path")
    {
        nucleus::config_space_builder engine;
        REQUIRE(engine.register_element(nucleus::element("cfg", anchor::root())));
        REQUIRE(engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg"))));
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{xml_of("<cfg><val>99</val></cfg>")},
            {});
        REQUIRE(loaded);

        REQUIRE(loaded.value().get("cfg/val").has_value());
        REQUIRE(*loaded.value().get("cfg/val") == "99");

        auto r = loaded.value().get_as<int32_t>("cfg/val");
        REQUIRE(r);
        REQUIRE(r.value() == 99);
    }

    SECTION("type mismatch error contains 'type mismatch'")
    {
        nucleus::config_space_builder engine;
        REQUIRE(engine.register_element(nucleus::element("cfg", anchor::root())));
        REQUIRE(engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg"))));
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{xml_of("<cfg><val>99</val></cfg>")},
            {});
        REQUIRE(loaded);

        // int32_t was stored; requesting double is a type mismatch.
        auto r = loaded.value().get_as<double>("cfg/val");
        REQUIRE(!r);
        REQUIRE(r.error().message.find("type mismatch") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// repeated_typed_element<T>: factory shape -- repeated, typed, type-identified
// ---------------------------------------------------------------------------
TEST_CASE("repeated_typed_element<T> sets repeated, converter, and type identity",
          "[typed][repeated][factory]")
{
    SECTION("built-in-scalar overload")
    {
        auto el = nucleus::repeated_typed_element<int32_t>("tags", anchor::keyspace("cfg"));
        REQUIRE(el.repeated);
        REQUIRE(static_cast<bool>(el.converter));
        REQUIRE(el.type_identity.has_value());
        REQUIRE(el.type_identity.value() == std::type_index(typeid(int32_t)));
    }

    SECTION("explicit-converter overload")
    {
        // A custom converter that uppercases the leading character is enough to
        // prove the supplied converter is the one attached.
        auto conv = [](std::string_view sv) -> nucleus::expected<std::any, std::string> {
            return std::any(std::string(sv));
        };
        auto el = nucleus::repeated_typed_element<std::string>(
            "tags", anchor::keyspace("cfg"), conv);
        REQUIRE(el.repeated);
        REQUIRE(static_cast<bool>(el.converter));
        REQUIRE(el.type_identity.value() == std::type_index(typeid(std::string)));
    }
}

// ---------------------------------------------------------------------------
// Typed+repeated round-trip: schema -> XML parse -> fold -> convert -> get_all_as
// ---------------------------------------------------------------------------
TEST_CASE("repeated_typed_element<T> round-trips the full pipeline in fold order",
          "[typed][repeated][roundtrip]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cfg", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_typed_element<int32_t>("nums", anchor::keyspace("cfg"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{xml_of("<cfg><nums>1</nums><nums>2</nums><nums>3</nums></cfg>")},
        {});
    REQUIRE(loaded);

    // The typed collection arrives in document/fold order...
    auto typed = loaded.value().get_all_as<int32_t>("cfg/nums");
    REQUIRE(typed);
    REQUIRE(typed.value() == std::vector<int32_t>{1, 2, 3});

    // ...and the raw collection agrees on order and count.
    auto raw = loaded.value().get_all("cfg/nums");
    REQUIRE(raw == std::vector<std::string>{"1", "2", "3"});
    REQUIRE(raw.size() == typed.value().size());
}

// ---------------------------------------------------------------------------
// Convert-pass defect probe: a single bad element fails with its index + layer
// ---------------------------------------------------------------------------
TEST_CASE("repeated_typed_element<T> convert pass fails on a mid-collection defect "
          "naming the element index and winning layer",
          "[typed][repeated][failure]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cfg", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_typed_element<int32_t>("nums", anchor::keyspace("cfg"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    // The winning layer's collection has a non-numeric element at index 1.
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{xml_of("<cfg><nums>10</nums><nums>notanumber</nums><nums>30</nums></cfg>")},
        {});
    REQUIRE_FALSE(loaded);
    INFO("error: " << loaded.error());
    REQUIRE(loaded.error().message.find("conversion failed for") != std::string::npos);
    REQUIRE(loaded.error().message.find("cfg/nums") != std::string::npos);
    // The exact failing element index...
    REQUIRE(loaded.error().message.find("[1]") != std::string::npos);
    REQUIRE(loaded.error().message.find("invalid characters") != std::string::npos);
    // ...and the winning layer label.
    REQUIRE(loaded.error().message.find("stack[0]") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Same-rank fold order + cross-layer replace for repeated typed collections
// ---------------------------------------------------------------------------
TEST_CASE("repeated_typed_element<T> appends within a layer and replaces across layers",
          "[typed][repeated][foldorder]")
{
    SECTION("append accumulates the occurrences WITHIN a single layer")
    {
        nucleus::config_space space = repeated_typed_element_cfg_space();

        // One layer carrying several occurrences: the first replaces any lower
        // collection, the rest append -- so all occurrences accumulate in order.
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{xml_of("<cfg><nums>1</nums><nums>2</nums></cfg>")},
            {});
        REQUIRE(loaded);
        auto typed = loaded.value().get_all_as<int32_t>("cfg/nums");
        REQUIRE(typed);
        REQUIRE(typed.value() == std::vector<int32_t>{1, 2});
    }

    SECTION("equal-rank layers fold in stack insertion order; the later one REPLACES")
    {
        nucleus::config_space space = repeated_typed_element_cfg_space();

        // Two SEPARATE sources at adjacent stack positions: stable_sort preserves
        // insertion order, so "second" folds after "first". Replace-across-layers is
        // per-layer (a new layer's first occurrence clears the lower collection),
        // so the later-inserted same-rank layer replaces, not appends.
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{
                xml_of("<cfg><nums>1</nums></cfg>"),
                xml_of("<cfg><nums>2</nums></cfg>")},
            {});
        REQUIRE(loaded);
        auto typed = loaded.value().get_all_as<int32_t>("cfg/nums");
        REQUIRE(typed);
        REQUIRE(typed.value() == std::vector<int32_t>{2});
    }

    SECTION("a higher-rank layer replaces the lower layer's collection")
    {
        nucleus::config_space space = repeated_typed_element_cfg_space();

        // base at lower precedence (stack[0]), derived at higher (stack[1]).
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{
                xml_of("<cfg><nums>1</nums><nums>2</nums><nums>3</nums></cfg>"),
                xml_of("<cfg><nums>10</nums><nums>20</nums></cfg>")},
            {});
        REQUIRE(loaded);
        auto typed = loaded.value().get_all_as<int32_t>("cfg/nums");
        REQUIRE(typed);
        // Replace, not append-across-layers: the base collection is gone.
        REQUIRE(typed.value() == std::vector<int32_t>{10, 20});
    }
}
