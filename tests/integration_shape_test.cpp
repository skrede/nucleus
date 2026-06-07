// Integration test: all v0.1.1 features exercised in one coherent fileset.
//
// Shape: cluster/server keyed by "name" (pkey), leaves port/protocol/serial.
// Domain: generic (cluster/server) -- no host vocabulary.
//
// Coverage:
//   - Keyed containers and anonymous templates composing across 3 XML files
//   - Named strain selection (select()) and auto-resolve (single named strain)
//   - Uniqueness enforcement (unique_element serial)
//   - Slice + prune: the resolved keyspace never contains a pkey segment
//   - Scope policies: file_level / space_open_container_closed (default) /
//     container_open_until_next_strain, all with XML inheritance chains
//   - Extend dispositions: narrow vs. wide in an inheritance chain
//   - Opt-out (inherit="none") truncates the chain
//   - Three loud-error paths: no-selection, unknown selection, duplicate unique value

#include "nucleus/configuration_space.h"
#include "nucleus/entry/configuration.h"
#include "nucleus/entry/strain_scope.h"
#include "nucleus/source/inherit_declaration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using nucleus::anchor;
using nucleus::strain_scope_policy;

namespace {

std::unique_ptr<nucleus::source> xml_of(const std::string &text)
{
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from_string(text));
}

// Returns the filename portion of a (possibly absolute) path string so factory
// lambdas can dispatch without knowledge of the working-directory prefix that
// weakly_canonical() prepends to relative paths when following inherit= links.
std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// Registers cluster/server keyed by "name" (pkey), leaves port/protocol,
// unique serial, plus a general app/name for scope-policy boundary tests.
void declare_cluster_with_unique(nucleus::configuration_space &engine)
{
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
    engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::unique_element("serial", anchor::keyspace("cluster/server")));
    engine.register_element(nucleus::element("app", anchor::root()));
    engine.register_element(nucleus::element("name", anchor::keyspace("app")));
}

// ---------------------------------------------------------------------------
// Shared document constants for the main 3-file chain (TC-1 and TC-6/TC-7).
// root.xml: anonymous template (port=9090) + named strain "yin" (port=8080,
//           serial=SN001). mid.xml inherits root.xml, adds protocol to the
//           anonymous template, wide-extends yin (port=443), introduces yang
//           (port=22, serial=SN002). leaf.xml inherits mid.xml, empty body.
// ---------------------------------------------------------------------------

const char *ROOT_DOC = R"(
    <cluster>
        <server><port>9090</port></server>
        <server name="yin" serial="SN001"><port>8080</port></server>
    </cluster>)";

const char *MID_DOC = R"(
    <cluster inherit="root.xml">
        <server><protocol>tcp</protocol></server>
        <server name="yin" extend="wide"><port>443</port></server>
        <server name="yang" serial="SN002"><port>22</port></server>
    </cluster>)";

const char *LEAF_DOC = R"(
    <cluster inherit="mid.xml">
    </cluster>)";

// Factory for the main 3-file chain.
auto make_main_factory()
{
    return [](const std::string &path) -> std::unique_ptr<nucleus::source> {
        const std::string name = filename_of(path);
        if(name == "root.xml")
            return xml_of(ROOT_DOC);
        if(name == "mid.xml")
            return xml_of(MID_DOC);
        if(name == "leaf.xml")
            return xml_of(LEAF_DOC);
        return nullptr;
    };
}

// ---------------------------------------------------------------------------
// TC-4 document constants: 3-doc chain for scope-policy contrast.
//   ROOT_DOC_TC4: yin, port=8080 only (Ld = ROOT rank).
//   MID_DOC_TC4:  yin with extend="narrow" + protocol="tcp" (rank = MID).
//   YANG_DOC_TC4: inherits MID_DOC_TC4, introduces yang (Ls = YANG rank).
// Rank ordering: ROOT < MID < YANG (each layer increments rank at load).
// yin's protocol entry_rank = MID, which satisfies Ld < MID < Ls = YANG.
// container_open_until_next_strain: MID < Ls -> protocol admitted.
// space_open_container_closed:      MID > Ld -> protocol excluded.
// ---------------------------------------------------------------------------

const char *ROOT_DOC_TC4 = R"(
    <cluster>
        <server name="yin"><port>8080</port></server>
    </cluster>)";

const char *MID_DOC_TC4 = R"(
    <cluster inherit="root_tc4.xml">
        <server name="yin" extend="narrow"><protocol>tcp</protocol></server>
    </cluster>)";

const char *YANG_DOC_TC4 = R"(
    <cluster inherit="mid_tc4.xml">
        <server name="yang" serial="SN002"><port>22</port></server>
    </cluster>)";

auto make_tc4_factory()
{
    return [](const std::string &path) -> std::unique_ptr<nucleus::source> {
        const std::string name = filename_of(path);
        if(name == "root_tc4.xml")
            return xml_of(ROOT_DOC_TC4);
        if(name == "mid_tc4.xml")
            return xml_of(MID_DOC_TC4);
        if(name == "yang_tc4.xml")
            return xml_of(YANG_DOC_TC4);
        return nullptr;
    };
}

} // namespace

// ---------------------------------------------------------------------------
// TC-1: select yin, full 3-doc chain; verify unified keyspace, no pkey segments.
// ---------------------------------------------------------------------------
TEST_CASE("integration: select yin resolves unified keyspace with template composition",
          "[integration][keyed]")
{
    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    REQUIRE(engine.select("yin"));

    auto loaded = engine.load(std::vector<std::string>{"leaf.xml"}, make_main_factory());
    REQUIRE(loaded);

    const nucleus::configuration &config = loaded.value();

    // Wide extend from mid wins over root's 8080.
    REQUIRE(config.get("cluster/server/port") == "443");

    // Anonymous template from mid (protocol="tcp") survives into the unified keyspace.
    REQUIRE(config.get("cluster/server/protocol") == "tcp");

    // Primary-key segment "yin" must never appear as a path segment.
    REQUIRE_FALSE(config.contains("cluster/server/yin/port"));

    // Serial "SN001" on yin is relayed to the unified path cluster/server/serial.
    REQUIRE(config.get("cluster/server/serial") == "SN001");
}

// ---------------------------------------------------------------------------
// TC-2: auto-resolve single named strain without select().
// ---------------------------------------------------------------------------
TEST_CASE("integration: auto-resolve single named strain succeeds without select()",
          "[integration][keyed]")
{
    // 2-doc chain where only "yin" is named across the whole chain.
    const char *base_doc = R"(
        <cluster>
            <server><port>9090</port></server>
            <server name="yin"><port>8080</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base2.xml">
        </cluster>)";

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::source> {
        const std::string name = filename_of(path);
        if(name == "base2.xml")
            return xml_of(base_doc);
        if(name == "derived2.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    // No select() -- single named strain auto-resolves.

    auto loaded = engine.load(std::vector<std::string>{"derived2.xml"}, factory);
    REQUIRE(loaded);

    // yin's port survives; anonymous template port (9090) is the fallback.
    REQUIRE(loaded.value().get("cluster/server/port") == "8080");
}

// ---------------------------------------------------------------------------
// TC-3: file_level scope policy excludes derived-layer keyed entries.
// Derived doc uses extend="narrow" (NOT wide) so relay_strain applies the
// rank filter. file_level excludes any entry whose rank > Ld.
// ---------------------------------------------------------------------------
TEST_CASE("integration: file_level scope policy excludes derived-layer entries",
          "[integration][keyed]")
{
    // root3.xml: yin's defining layer (Ld = root3 rank), port=8080.
    const char *root3_doc = R"(
        <cluster>
            <server name="yin"><port>8080</port></server>
        </cluster>)";

    // derived3.xml: narrow-extends yin, adds protocol in the derived layer (rank > Ld).
    // file_level excludes yin's protocol because its rank > Ld.
    const char *derived3_doc = R"(
        <cluster inherit="root3.xml">
            <server name="yin" extend="narrow"><protocol>tcp</protocol></server>
        </cluster>)";

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::source> {
        const std::string name = filename_of(path);
        if(name == "root3.xml")
            return xml_of(root3_doc);
        if(name == "derived3.xml")
            return xml_of(derived3_doc);
        return nullptr;
    };

    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    REQUIRE(engine.select("yin"));
    REQUIRE(engine.set_strain_scope(strain_scope_policy::file_level));

    auto loaded = engine.load(std::vector<std::string>{"derived3.xml"}, factory);
    REQUIRE(loaded);

    // Root value at Ld survives under file_level.
    REQUIRE(loaded.value().get("cluster/server/port") == "8080");

    // Derived-layer keyed entry excluded (rank > Ld, narrow-extend, file_level).
    REQUIRE_FALSE(loaded.value().contains("cluster/server/protocol"));
}

// ---------------------------------------------------------------------------
// TC-4: scope-policy contrast: container_open_until_next_strain vs.
//        space_open_container_closed, same fileset, opposite outcomes.
// yin's protocol (MID rank) satisfies Ld < MID < Ls = YANG rank.
// Sub-test A: container_open_until_next_strain -- rank in [Ld, Ls) -> admitted.
// Sub-test B: space_open_container_closed       -- rank > Ld         -> excluded.
// ---------------------------------------------------------------------------
TEST_CASE("integration: scope-policy contrast for yin's derived entry",
          "[integration][keyed]")
{
    SECTION("container_open_until_next_strain admits yin's extend up to Ls")
    {
        nucleus::configuration_space engine;
        declare_cluster_with_unique(engine);
        REQUIRE(engine.select("yin"));
        REQUIRE(engine.set_strain_scope(
            strain_scope_policy::container_open_until_next_strain));

        auto loaded =
            engine.load(std::vector<std::string>{"yang_tc4.xml"}, make_tc4_factory());
        REQUIRE(loaded);

        // yin's protocol rank (MID) is in [Ld, Ls) -- admitted.
        REQUIRE(loaded.value().contains("cluster/server/protocol"));

        // Non-selected strain yang must be pruned entirely.
        REQUIRE_FALSE(loaded.value().contains("cluster/server/yang/port"));
    }

    SECTION("space_open_container_closed excludes yin's derived entry")
    {
        nucleus::configuration_space engine;
        declare_cluster_with_unique(engine);
        REQUIRE(engine.select("yin"));
        REQUIRE(engine.set_strain_scope(strain_scope_policy::space_open_container_closed));

        auto loaded =
            engine.load(std::vector<std::string>{"yang_tc4.xml"}, make_tc4_factory());
        REQUIRE(loaded);

        // yin's protocol rank (MID) > Ld (ROOT) -- excluded.
        REQUIRE_FALSE(loaded.value().contains("cluster/server/protocol"));
    }
}

// ---------------------------------------------------------------------------
// TC-5: opt-out (inherit="none") truncates the chain; root entries absent.
// ---------------------------------------------------------------------------
TEST_CASE("integration: opt-out in mid truncates chain, root entries absent",
          "[integration][keyed]")
{
    // root5.xml: yin with port=9090 (root value, must become absent after truncation).
    const char *root5_doc = R"(
        <cluster>
            <server name="yin"><port>9090</port></server>
        </cluster>)";

    // mid5.xml: opts out of root, redefines yin with port=7070.
    const char *mid5_doc = R"(
        <cluster inherit="none">
            <server name="yin"><port>7070</port></server>
        </cluster>)";

    // leaf5.xml: inherits mid5, adds nothing.
    const char *leaf5_doc = R"(
        <cluster inherit="mid5.xml">
        </cluster>)";

    bool root5_accessed = false;

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::source> {
        const std::string name = filename_of(path);
        if(name == "root5.xml")
        {
            root5_accessed = true;
            return xml_of(root5_doc);
        }
        if(name == "mid5.xml")
            return xml_of(mid5_doc);
        if(name == "leaf5.xml")
            return xml_of(leaf5_doc);
        return nullptr;
    };

    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    REQUIRE(engine.select("yin"));

    auto loaded = engine.load(std::vector<std::string>{"leaf5.xml"}, factory);
    REQUIRE(loaded);

    // Root was never fetched (opt-out at mid5 truncated the walk).
    REQUIRE_FALSE(root5_accessed);

    // mid's value survives; root's value is absent.
    REQUIRE(loaded.value().get("cluster/server/port") == "7070");
}

// ---------------------------------------------------------------------------
// TC-6: multiple strains with no selection is a loud error.
// ---------------------------------------------------------------------------
TEST_CASE("integration: multiple strains with no selection is a loud error",
          "[integration][keyed]")
{
    // 2-doc chain with both yin and yang; no select() call.
    const char *base6_doc = R"(
        <cluster>
            <server name="yin"><port>8080</port></server>
        </cluster>)";

    const char *derived6_doc = R"(
        <cluster inherit="base6.xml">
            <server name="yang"><port>22</port></server>
        </cluster>)";

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::source> {
        const std::string name = filename_of(path);
        if(name == "base6.xml")
            return xml_of(base6_doc);
        if(name == "derived6.xml")
            return xml_of(derived6_doc);
        return nullptr;
    };

    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    // No select() -- multiple strains without selection must be a loud error.

    auto loaded = engine.load(std::vector<std::string>{"derived6.xml"}, factory);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("no instance is selected") != std::string::npos);
}

// ---------------------------------------------------------------------------
// TC-7: select with unknown key value is a loud error.
// ---------------------------------------------------------------------------
TEST_CASE("integration: select with unknown key value is a loud error",
          "[integration][keyed]")
{
    // 2-doc chain with yin and yang; select "ghost" which does not exist.
    const char *base7_doc = R"(
        <cluster>
            <server name="yin"><port>8080</port></server>
        </cluster>)";

    const char *derived7_doc = R"(
        <cluster inherit="base7.xml">
            <server name="yang"><port>22</port></server>
        </cluster>)";

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::source> {
        const std::string name = filename_of(path);
        if(name == "base7.xml")
            return xml_of(base7_doc);
        if(name == "derived7.xml")
            return xml_of(derived7_doc);
        return nullptr;
    };

    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    REQUIRE(engine.select("ghost"));

    auto loaded = engine.load(std::vector<std::string>{"derived7.xml"}, factory);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("does not match any strain") != std::string::npos);
}

// ---------------------------------------------------------------------------
// TC-8: duplicate unique field value across strains is a loud error.
// Both yin and yang carry serial="SN001". select("yin") is required so the
// multiple-strains-no-selection guard does not fire before Step C. Step C
// iterates all strain buckets (computed before pruning), so the duplicate
// across yin and yang is still detected.
// ---------------------------------------------------------------------------
TEST_CASE("integration: duplicate unique field value across strains is a loud error",
          "[integration][keyed]")
{
    // Single doc: yin and yang both carry serial="SN001" (duplicate unique value).
    const char *doc8 = R"(
        <cluster>
            <server name="yin" serial="SN001"><port>8080</port></server>
            <server name="yang" serial="SN001"><port>22</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    // Must call select("yin") so Step C (unique check) runs before the
    // multiple-strains-no-selection guard, which would fire first otherwise.
    REQUIRE(engine.select("yin"));

    auto loaded = engine.load(std::vector<std::string>{"doc8.xml"},
                              [&](const std::string &) { return xml_of(doc8); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("unique field") != std::string::npos);
    REQUIRE(loaded.error().find("SN001") != std::string::npos);
}
