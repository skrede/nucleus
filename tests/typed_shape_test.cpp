// Interaction shape tests: typed seam x composition model
// (inheritance chain, strain selection, repeated across layers, scope policy,
// access surface). Adds ONLY shapes not covered by typed_element_test.cpp.

#include "nucleus/configuration_space.h"

#include "nucleus/entry/strain_scope.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <optional>
#include <typeindex>
#include <string_view>

using nucleus::anchor;
using nucleus::strain_scope_policy;

namespace {

std::unique_ptr<nucleus::configuration_source> xml_of(const std::string &text)
{
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from(nucleus::xml::xml_source_options::of_string(text)));
}

std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

void declare_server_typed(nucleus::configuration_space_builder &engine)
{
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::typed_element<int32_t>("port", anchor::keyspace("cluster/server")));
}

void add_layer(nucleus::source_stack_options &opts, nucleus::configuration_source &src,
               std::size_t rank, std::string label)
{
    opts.custom_layers.push_back(
        nucleus::configuration_source_layer{&src, rank, std::move(label), {}});
}

nucleus::load_result load_chain(const nucleus::configuration_space &space,
                                std::vector<std::string> paths,
                                nucleus::document_factory factory,
                                std::optional<std::string> selection = std::nullopt)
{
    nucleus::source_stack_options opts;
    opts.document_paths = std::move(paths);
    opts.make_document = std::move(factory);
    opts.selection = std::move(selection);
    return nucleus::load_configuration(space, opts);
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

    nucleus::configuration_space_builder engine;
    declare_server_typed(engine);
    nucleus::configuration_space space = engine.build();

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
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

    nucleus::configuration_space_builder engine;
    declare_server_typed(engine);
    nucleus::configuration_space space = engine.build();

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
    REQUIRE(!loaded);
    INFO("error: " << loaded.error());
    // The convert() format is "conversion failed for '...': ... (layer: ...)"
    REQUIRE(loaded.error().find("conversion failed for") != std::string::npos);
    REQUIRE(loaded.error().find("cluster/server/port") != std::string::npos);
    REQUIRE(loaded.error().find("invalid characters") != std::string::npos);
    REQUIRE(loaded.error().find("derived") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Typed x pruned strain: selected strain resolves; bad value in pruned strain
// does not fail
// ---------------------------------------------------------------------------
TEST_CASE("typed x pruned strain: selected strain resolves; bad value in pruned strain does not fail",
          "[typed][strain][pruned]")
{
    // Document with two named strains: yin (port=80) and yang (port=notanumber).
    const char *doc = R"(
        <cluster>
            <server name="yin"><port>80</port></server>
            <server name="yang"><port>notanumber</port></server>
        </cluster>)";

    SECTION("selecting the good strain succeeds")
    {
        nucleus::configuration_space_builder engine;
        declare_server_typed(engine);
        nucleus::configuration_space space = engine.build();

        auto src = xml_of(doc);
        nucleus::source_stack_options opts;
        add_layer(opts, *src, 10, "doc");
        opts.selection = "yin";

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);

        auto p = loaded.value().get_as<int32_t>("cluster/server/port");
        REQUIRE(p);
        REQUIRE(p.value() == 80);
    }

    SECTION("selecting the bad strain fails")
    {
        nucleus::configuration_space_builder engine;
        declare_server_typed(engine);
        nucleus::configuration_space space = engine.build();

        auto src = xml_of(doc);
        nucleus::source_stack_options opts;
        add_layer(opts, *src, 10, "doc");
        opts.selection = "yang";

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(!loaded);
        REQUIRE(loaded.error().find("invalid characters") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Typed x repeated across layers: winning layer collection replaces base
// ---------------------------------------------------------------------------
TEST_CASE("typed x repeated across layers: winning layer collection replaces base",
          "[typed][repeated][layers]")
{
    SECTION("winning collection converts fully")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        auto el = nucleus::typed_element<int32_t>("nums", anchor::keyspace("cfg"));
        el.repeated = true;
        engine.register_element(el);
        nucleus::configuration_space space = engine.build();

        auto base_src = xml_of(
            "<cfg><nums>1</nums><nums>2</nums><nums>3</nums></cfg>");
        auto derived_src = xml_of(
            "<cfg><nums>10</nums><nums>20</nums></cfg>");

        nucleus::source_stack_options opts;
        add_layer(opts, *base_src, 10, "base");
        add_layer(opts, *derived_src, 20, "derived");

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);

        auto r = loaded.value().get_all_as<int32_t>("cfg/nums");
        REQUIRE(r);
        REQUIRE(r.value() == std::vector<int32_t>{10, 20});

        auto s = loaded.value().get_all("cfg/nums");
        REQUIRE(s == std::vector<std::string>{"10", "20"});
    }

    SECTION("bad element in winning collection fails with element index")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        auto el = nucleus::typed_element<int32_t>("nums", anchor::keyspace("cfg"));
        el.repeated = true;
        engine.register_element(el);
        nucleus::configuration_space space = engine.build();

        auto base_src = xml_of(
            "<cfg><nums>1</nums><nums>2</nums><nums>3</nums></cfg>");
        auto derived_src = xml_of(
            "<cfg><nums>10</nums><nums>notanumber</nums></cfg>");

        nucleus::source_stack_options opts;
        add_layer(opts, *base_src, 10, "base");
        add_layer(opts, *derived_src, 20, "derived");

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(!loaded);
        INFO("error: " << loaded.error());
        REQUIRE(loaded.error().find("cfg/nums") != std::string::npos);
        REQUIRE(loaded.error().find("[1]") != std::string::npos);
        REQUIRE(loaded.error().find("derived") != std::string::npos);
    }

    SECTION("bad element only in losing collection does not fail")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        auto el = nucleus::typed_element<int32_t>("nums", anchor::keyspace("cfg"));
        el.repeated = true;
        engine.register_element(el);
        nucleus::configuration_space space = engine.build();

        auto base_src = xml_of(
            "<cfg><nums>1</nums><nums>notanumber</nums></cfg>");
        auto derived_src = xml_of(
            "<cfg><nums>10</nums><nums>20</nums></cfg>");

        nucleus::source_stack_options opts;
        add_layer(opts, *base_src, 10, "base");
        add_layer(opts, *derived_src, 20, "derived");

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);

        auto r = loaded.value().get_all_as<int32_t>("cfg/nums");
        REQUIRE(r);
        REQUIRE(r.value() == std::vector<int32_t>{10, 20});
    }
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

    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::typed_element<int32_t>("port", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::typed_element<int32_t>("score", anchor::keyspace("cluster/server")));
    nucleus::configuration_space space = engine.build();

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
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
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg")));
        nucleus::configuration_space space = engine.build();

        auto src = xml_of("<cfg><val>99</val></cfg>");
        nucleus::source_stack_options opts;
        add_layer(opts, *src, 10, "doc");

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);

        REQUIRE(loaded.value().get("cfg/val").has_value());
        REQUIRE(*loaded.value().get("cfg/val") == "99");

        auto r = loaded.value().get_as<int32_t>("cfg/val");
        REQUIRE(r);
        REQUIRE(r.value() == 99);
    }

    SECTION("type mismatch error contains 'type mismatch'")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg")));
        nucleus::configuration_space space = engine.build();

        auto src = xml_of("<cfg><val>99</val></cfg>");
        nucleus::source_stack_options opts;
        add_layer(opts, *src, 10, "doc");

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);

        // int32_t was stored; requesting double is a type mismatch.
        auto r = loaded.value().get_as<double>("cfg/val");
        REQUIRE(!r);
        REQUIRE(r.error().find("type mismatch") != std::string::npos);
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
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("cfg", anchor::root()));
    engine.register_element(
        nucleus::repeated_typed_element<int32_t>("nums", anchor::keyspace("cfg")));
    nucleus::configuration_space space = engine.build();

    auto src = xml_of("<cfg><nums>1</nums><nums>2</nums><nums>3</nums></cfg>");
    nucleus::source_stack_options opts;
    add_layer(opts, *src, 10, "doc");

    auto loaded = nucleus::load_configuration(space, opts);
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
    nucleus::configuration_space_builder engine;
    engine.register_element(nucleus::element("cfg", anchor::root()));
    engine.register_element(
        nucleus::repeated_typed_element<int32_t>("nums", anchor::keyspace("cfg")));
    nucleus::configuration_space space = engine.build();

    // The winning layer's collection has a non-numeric element at index 1.
    auto src = xml_of("<cfg><nums>10</nums><nums>notanumber</nums><nums>30</nums></cfg>");
    nucleus::source_stack_options opts;
    add_layer(opts, *src, 20, "winning-layer");

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE_FALSE(loaded);
    INFO("error: " << loaded.error());
    REQUIRE(loaded.error().find("conversion failed for") != std::string::npos);
    REQUIRE(loaded.error().find("cfg/nums") != std::string::npos);
    // The exact failing element index...
    REQUIRE(loaded.error().find("[1]") != std::string::npos);
    REQUIRE(loaded.error().find("invalid characters") != std::string::npos);
    // ...and the winning layer label.
    REQUIRE(loaded.error().find("winning-layer") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Same-rank fold order + cross-layer replace for repeated typed collections
// ---------------------------------------------------------------------------
TEST_CASE("repeated_typed_element<T> appends within a layer and replaces across layers",
          "[typed][repeated][foldorder]")
{
    SECTION("append accumulates the occurrences WITHIN a single layer")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::repeated_typed_element<int32_t>("nums", anchor::keyspace("cfg")));
        nucleus::configuration_space space = engine.build();

        // One layer carrying several occurrences: the first replaces any lower
        // collection, the rest append -- so all occurrences accumulate in order.
        auto single = xml_of("<cfg><nums>1</nums><nums>2</nums></cfg>");
        nucleus::source_stack_options opts;
        add_layer(opts, *single, 10, "single");

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);
        auto typed = loaded.value().get_all_as<int32_t>("cfg/nums");
        REQUIRE(typed);
        REQUIRE(typed.value() == std::vector<int32_t>{1, 2});
    }

    SECTION("equal-rank layers fold in stack insertion order; the later one REPLACES")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::repeated_typed_element<int32_t>("nums", anchor::keyspace("cfg")));
        nucleus::configuration_space space = engine.build();

        // Two SEPARATE layers at the same rank: stable_sort preserves insertion
        // order, so "second" folds after "first". Replace-across-layers is
        // per-layer (a new layer's first occurrence clears the lower collection),
        // so the later-inserted same-rank layer replaces, not appends.
        auto first = xml_of("<cfg><nums>1</nums></cfg>");
        auto second = xml_of("<cfg><nums>2</nums></cfg>");
        nucleus::source_stack_options opts;
        add_layer(opts, *first, 10, "first");
        add_layer(opts, *second, 10, "second");

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);
        auto typed = loaded.value().get_all_as<int32_t>("cfg/nums");
        REQUIRE(typed);
        REQUIRE(typed.value() == std::vector<int32_t>{2});
    }

    SECTION("a higher-rank layer replaces the lower layer's collection")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::repeated_typed_element<int32_t>("nums", anchor::keyspace("cfg")));
        nucleus::configuration_space space = engine.build();

        auto base = xml_of("<cfg><nums>1</nums><nums>2</nums><nums>3</nums></cfg>");
        auto derived = xml_of("<cfg><nums>10</nums><nums>20</nums></cfg>");
        nucleus::source_stack_options opts;
        add_layer(opts, *base, 10, "base");
        add_layer(opts, *derived, 20, "derived");

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);
        auto typed = loaded.value().get_all_as<int32_t>("cfg/nums");
        REQUIRE(typed);
        // Replace, not append-across-layers: the base collection is gone.
        REQUIRE(typed.value() == std::vector<int32_t>{10, 20});
    }
}
