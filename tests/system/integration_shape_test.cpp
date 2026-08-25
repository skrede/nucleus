// Integration test: the keying, composition, and inheritance features
// exercised together in one coherent fileset.
//
// Shape: cluster/server keyed by "name" (pkey), leaves port/protocol/serial.
// Domain: generic (cluster/server) -- no host vocabulary.
//
// Coverage:
//   - Keyed containers and anonymous templates composing across 3 XML files
//   - Named strain selection (per-load) and auto-resolve (single named strain)
//   - Uniqueness enforcement (unique_element serial)
//   - Slice + prune: the resolved keyspace never contains a pkey segment
//   - Scope policies: file_level / space_open_container_closed (default) /
//     container_open_until_next_strain, all with XML inheritance chains
//   - Extend dispositions: narrow vs. wide in an inheritance chain
//   - Opt-out (inherit="none") truncates the chain
//   - Three loud-error paths: no-selection, unknown selection, duplicate unique value

#include "nucleus/strain_scope.h"
#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include "nucleus/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <optional>
#include <functional>

using nucleus::anchor;
using nucleus::strain_scope_policy;

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// Registers cluster/server keyed by "name" (pkey), leaves port/protocol,
// unique serial, plus a general app/name for scope-policy boundary tests.
void declare_cluster_with_unique(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::unique_element("serial", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("app", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("name", anchor::keyspace("app"))));
}

// Loads a document chain against `space` carrying per-load selection and scope.
nucleus::load_result load_chain(const nucleus::config_space &space,
                                std::vector<std::string> paths,
                                std::function<nucleus::source_handle(const std::string &)> factory,
                                std::optional<std::string> selection = std::nullopt,
                                strain_scope_policy scope = strain_scope_policy::space_open_container_closed)
{
    nucleus::load_options opts;
    opts.document_paths = std::move(paths);
    opts.make_document = std::move(factory);
    opts.selection = std::move(selection);
    opts.scope = scope;
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

// ---------------------------------------------------------------------------
// Shared document constants for the main 3-file chain.
// ---------------------------------------------------------------------------

const char *ROOT_DOC = R"(
    <cluster>
        <server><port>9090</port></server>
        <server name="primary" serial="SN001"><port>8080</port></server>
    </cluster>)";

const char *MID_DOC = R"(
    <cluster inherit="root.xml">
        <server><protocol>tcp</protocol></server>
        <server name="primary" extend="wide"><port>443</port></server>
        <server name="secondary" serial="SN002"><port>22</port></server>
    </cluster>)";

const char *LEAF_DOC = R"(
    <cluster inherit="mid.xml">
    </cluster>)";

auto make_main_factory()
{
    return [](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "root.xml")
            return xml_of(ROOT_DOC);
        if(name == "mid.xml")
            return xml_of(MID_DOC);
        if(name == "leaf.xml")
            return xml_of(LEAF_DOC);
        return nucleus::source_handle(nucleus::env_source{});
    };
}

// ---------------------------------------------------------------------------
// Document constants: 3-doc chain for scope-policy contrast.
// ---------------------------------------------------------------------------

const char *ROOT_DOC_TC4 = R"(
    <cluster>
        <server name="primary"><port>8080</port></server>
    </cluster>)";

const char *MID_DOC_TC4 = R"(
    <cluster inherit="root_tc4.xml">
        <server name="primary" extend="narrow"><protocol>tcp</protocol></server>
    </cluster>)";

const char *YANG_DOC_TC4 = R"(
    <cluster inherit="mid_tc4.xml">
        <server name="secondary" serial="SN002"><port>22</port></server>
    </cluster>)";

auto make_tc4_factory()
{
    return [](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "root_tc4.xml")
            return xml_of(ROOT_DOC_TC4);
        if(name == "mid_tc4.xml")
            return xml_of(MID_DOC_TC4);
        if(name == "yang_tc4.xml")
            return xml_of(YANG_DOC_TC4);
        return nucleus::source_handle(nucleus::env_source{});
    };
}

}

// ---------------------------------------------------------------------------
// Select primary, full 3-doc chain; verify unified keyspace, no pkey segments.
// ---------------------------------------------------------------------------
TEST_CASE("integration: select primary resolves unified keyspace with template composition",
          "[integration][keyed]")
{
    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_chain(space, {"leaf.xml"}, make_main_factory(), "primary");
    REQUIRE(loaded);

    const nucleus::config &config = loaded.value();

    // Wide extend from mid wins over root's 8080.
    REQUIRE(config.get("cluster/server/port") == "443");

    // Anonymous template from mid (protocol="tcp") survives into the unified keyspace.
    REQUIRE(config.get("cluster/server/protocol") == "tcp");

    // Primary-key segment "primary" must never appear as a path segment.
    REQUIRE_FALSE(config.contains("cluster/server/primary/port"));

    // Serial "SN001" on primary is relayed to the unified path cluster/server/serial.
    REQUIRE(config.get("cluster/server/serial") == "SN001");
}

// ---------------------------------------------------------------------------
// auto-resolve single named strain without a selection.
// ---------------------------------------------------------------------------
TEST_CASE("integration: auto-resolve single named strain succeeds without selection",
          "[integration][keyed]")
{
    const char *base_doc = R"(
        <cluster>
            <server><port>9090</port></server>
            <server name="primary"><port>8080</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base2.xml">
        </cluster>)";

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base2.xml")
            return xml_of(base_doc);
        if(name == "derived2.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);
    // No selection -- single named strain auto-resolves.

    auto loaded = load_chain(space, {"derived2.xml"}, factory);
    REQUIRE(loaded);

    // primary's port survives; anonymous template port (9090) is the fallback.
    REQUIRE(loaded.value().get("cluster/server/port") == "8080");
}

// ---------------------------------------------------------------------------
// file_level scope policy excludes derived-layer keyed entries.
// ---------------------------------------------------------------------------
TEST_CASE("integration: file_level scope policy excludes derived-layer entries",
          "[integration][keyed]")
{
    const char *root3_doc = R"(
        <cluster>
            <server name="primary"><port>8080</port></server>
        </cluster>)";

    const char *derived3_doc = R"(
        <cluster inherit="root3.xml">
            <server name="primary" extend="narrow"><protocol>tcp</protocol></server>
        </cluster>)";

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "root3.xml")
            return xml_of(root3_doc);
        if(name == "derived3.xml")
            return xml_of(derived3_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_chain(space, {"derived3.xml"}, factory, "primary",
                             strain_scope_policy::file_level);
    REQUIRE(loaded);

    // Root value at Ld survives under file_level.
    REQUIRE(loaded.value().get("cluster/server/port") == "8080");

    // Derived-layer keyed entry excluded (rank > Ld, narrow-extend, file_level).
    REQUIRE_FALSE(loaded.value().contains("cluster/server/protocol"));
}

// ---------------------------------------------------------------------------
// scope-policy contrast: container_open_until_next_strain vs.
//        space_open_container_closed, same fileset, opposite outcomes.
// ---------------------------------------------------------------------------
TEST_CASE("integration: scope-policy contrast for primary's derived entry",
          "[integration][keyed]")
{
    SECTION("container_open_until_next_strain admits primary's extend up to Ls")
    {
        nucleus::config_space_builder engine;
        declare_cluster_with_unique(engine);
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        auto loaded = load_chain(space, {"yang_tc4.xml"}, make_tc4_factory(), "primary",
                                 strain_scope_policy::container_open_until_next_strain);
        REQUIRE(loaded);

        // primary's protocol rank (MID) is in [Ld, Ls) -- admitted.
        REQUIRE(loaded.value().contains("cluster/server/protocol"));

        REQUIRE(loaded.value().get("cluster/server/port") == "8080");
    }

    SECTION("space_open_container_closed excludes primary's derived entry")
    {
        nucleus::config_space_builder engine;
        declare_cluster_with_unique(engine);
        nucleus::config_space space = nucleus::builder_result_test::built(engine);

        auto loaded = load_chain(space, {"yang_tc4.xml"}, make_tc4_factory(), "primary",
                                 strain_scope_policy::space_open_container_closed);
        REQUIRE(loaded);

        // primary's protocol rank (MID) > Ld (ROOT) -- excluded.
        REQUIRE_FALSE(loaded.value().contains("cluster/server/protocol"));

        // The exclusion is surgical: primary's defining-layer entry still resolves.
        REQUIRE(loaded.value().get("cluster/server/port") == "8080");
    }
}

// ---------------------------------------------------------------------------
// opt-out (inherit="none") parses as the explicit chain terminator.
// ---------------------------------------------------------------------------
TEST_CASE("integration: opt-out terminates the chain by declaration",
          "[integration][keyed]")
{
    const char *mid5_doc = R"(
        <cluster inherit="none">
            <server name="primary"><port>7070</port></server>
        </cluster>)";

    const char *leaf5_doc = R"(
        <cluster inherit="mid5.xml">
        </cluster>)";

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "mid5.xml")
            return xml_of(mid5_doc);
        if(name == "leaf5.xml")
            return xml_of(leaf5_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_chain(space, {"leaf5.xml"}, factory, "primary");
    REQUIRE(loaded);

    // The chain ends at mid5 by explicit opt-out; mid's value resolves.
    REQUIRE(loaded.value().get("cluster/server/port") == "7070");
}

// ---------------------------------------------------------------------------
// Multiple strains with no selection is a loud error.
// ---------------------------------------------------------------------------
TEST_CASE("integration: multiple strains with no selection is a loud error",
          "[integration][keyed]")
{
    const char *base6_doc = R"(
        <cluster>
            <server name="primary"><port>8080</port></server>
        </cluster>)";

    const char *derived6_doc = R"(
        <cluster inherit="base6.xml">
            <server name="secondary"><port>22</port></server>
        </cluster>)";

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base6.xml")
            return xml_of(base6_doc);
        if(name == "derived6.xml")
            return xml_of(derived6_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);
    // No selection -- multiple strains without selection must be a loud error.

    auto loaded = load_chain(space, {"derived6.xml"}, factory);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("no instance is selected") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Select with unknown key value is a loud error.
// ---------------------------------------------------------------------------
TEST_CASE("integration: select with unknown key value is a loud error",
          "[integration][keyed]")
{
    const char *base7_doc = R"(
        <cluster>
            <server name="primary"><port>8080</port></server>
        </cluster>)";

    const char *derived7_doc = R"(
        <cluster inherit="base7.xml">
            <server name="secondary"><port>22</port></server>
        </cluster>)";

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base7.xml")
            return xml_of(base7_doc);
        if(name == "derived7.xml")
            return xml_of(derived7_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_chain(space, {"derived7.xml"}, factory, "ghost");
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("does not match any strain") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Duplicate unique field value across strains is a loud error.
// ---------------------------------------------------------------------------
TEST_CASE("integration: duplicate unique field value across strains is a loud error",
          "[integration][keyed]")
{
    const char *doc8 = R"(
        <cluster>
            <server name="primary" serial="SN001"><port>8080</port></server>
            <server name="secondary" serial="SN001"><port>22</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    // Select "primary" so the unique check runs before the multiple-strains-no-selection
    // guard, which would fire first otherwise.
    auto loaded = load_chain(space, {"doc8.xml"},
                             [&](const std::string &) { return xml_of(doc8); }, "primary");
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("unique field") != std::string::npos);
    REQUIRE(loaded.error().message.find("SN001") != std::string::npos);
}
