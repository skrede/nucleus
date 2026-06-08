#include "nucleus/configuration_space.h"
#include "nucleus/entry/configuration.h"
#include "nucleus/configuration_source/inherit_declaration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

// Inheritance chain walk, composition, extend dispositions, and duplicate
// detection tests. All tests use the factory-lambda in-memory pattern: no
// filesystem access, no host vocabulary; all shapes are generic (cluster/server).

using nucleus::anchor;

namespace {

std::unique_ptr<nucleus::configuration_source> xml_of(const std::string &text)
{
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from_string(text));
}

// cluster/server keyed by "name"; leaves: port, protocol.
void declare_cluster(nucleus::configuration_space &engine)
{
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
    engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server")));
}

// Same as declare_cluster plus a unique (non-identity) "serial" field.
void declare_cluster_with_unique(nucleus::configuration_space &engine)
{
    declare_cluster(engine);
    engine.register_element(
        nucleus::unique_element("serial", anchor::keyspace("cluster/server")));
}

// Returns the filename portion of a (possibly absolute) path string so factory
// lambdas can dispatch without knowledge of the working-directory prefix that
// weakly_canonical() prepends to relative paths when following inherit= links.
std::string filename_of(const std::string &path)
{
    // Find the last separator character (forward or backward slash).
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

}

// ---------------------------------------------------------------------------
// 1. Single-file, no inherit= -- backward compatibility baseline.
// ---------------------------------------------------------------------------
TEST_CASE("single file with no inherit attribute resolves normally", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));

    auto factory = [&](const std::string &) { return xml_of(base_doc); };
    auto loaded = engine.load(std::vector<std::string>{"base.xml"}, factory);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
    REQUIRE_FALSE(loaded.value().contains("cluster/server/web/port"));
}

// ---------------------------------------------------------------------------
// 2. Two-file chain: base defines strain; derived extends it with extra leaf.
// ---------------------------------------------------------------------------
TEST_CASE("two-file chain assembles root-first, derived overrides root", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><port>8080</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE(loaded);
    // derived.xml ranks above base.xml; its port=8080 wins.
    REQUIRE(loaded.value().get("cluster/server/port") == "8080");
}

// ---------------------------------------------------------------------------
// 3. Anonymous instances compose across two chain files.
// ---------------------------------------------------------------------------
TEST_CASE("anonymous instances compose across chain in document order", "[chain]")
{
    // No primary key declared -- anonymous instances compose directly.
    const char *base_doc = R"(
        <cluster>
            <server><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::configuration_space engine;
    // Schema without identity: anonymous content composes by rank.
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server")));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE(loaded);
    // Both layers contribute; derived (higher rank) supplies protocol.
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
    REQUIRE(loaded.value().get("cluster/server/protocol") == "tcp");
}

// ---------------------------------------------------------------------------
// 4. Named strain in derived composes on anonymous template from base.
// ---------------------------------------------------------------------------
TEST_CASE("named strain in derived composes on template from root", "[chain]")
{
    // base.xml: anonymous template with port=9090.
    const char *base_doc = R"(
        <cluster>
            <server><port>9090</port></server>
        </cluster>)";

    // derived.xml: named strain "web" with protocol=tcp.
    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web"><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    // Single named strain auto-resolves; no select() call needed.

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE(loaded);
    // Template (anonymous, lower rank) supplies port; named strain supplies protocol.
    REQUIRE(loaded.value().get("cluster/server/port") == "9090");
    REQUIRE(loaded.value().get("cluster/server/protocol") == "tcp");
}

// ---------------------------------------------------------------------------
// 5. Opt-out (inherit=none) truncates the chain; grandparent never fetched.
// ---------------------------------------------------------------------------
TEST_CASE("opt-out truncates chain below declaring file", "[chain]")
{
    const char *grandparent_doc = R"(
        <cluster>
            <server name="root"><port>1</port></server>
        </cluster>)";

    const char *base_doc = R"(
        <cluster inherit="none">
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><port>8080</port></server>
        </cluster>)";

    bool grandparent_accessed = false;

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "grandparent.xml")
        {
            grandparent_accessed = true;
            return xml_of(grandparent_doc);
        }
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE(loaded);
    // The grandparent was never fetched because base declared inherit="none".
    REQUIRE_FALSE(grandparent_accessed);
    REQUIRE(loaded.value().get("cluster/server/port") == "8080");
}

// ---------------------------------------------------------------------------
// 6. Depth cap exceeded returns a loud error.
// ---------------------------------------------------------------------------
TEST_CASE("depth cap exceeded returns loud error naming the limit", "[chain]")
{
    // Three-file chain: a.xml -> b.xml -> c.xml. With cap=2 the third push fails.
    const char *a_doc = R"(<cluster inherit="b.xml"><server name="a"><port>1</port></server></cluster>)";
    const char *b_doc = R"(<cluster inherit="c.xml"><server name="b"><port>2</port></server></cluster>)";
    const char *c_doc = R"(<cluster><server name="c"><port>3</port></server></cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    nucleus::inherit_policy policy;
    policy.depth_cap = 2;
    REQUIRE(engine.set_inherit_policy(std::move(policy)));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "a.xml")
            return xml_of(a_doc);
        if(name == "b.xml")
            return xml_of(b_doc);
        if(name == "c.xml")
            return xml_of(c_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"a.xml"}, factory);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("depth") != std::string::npos);
    REQUIRE(loaded.error().find("2") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 7. Cycle detection returns a loud error naming the cycling path.
// ---------------------------------------------------------------------------
TEST_CASE("cycle in inheritance chain fails loudly naming the path", "[chain]")
{
    const char *a_doc = R"(<cluster inherit="b.xml"><server name="a"><port>1</port></server></cluster>)";
    const char *b_doc = R"(<cluster inherit="a.xml"><server name="b"><port>2</port></server></cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "a.xml")
            return xml_of(a_doc);
        if(name == "b.xml")
            return xml_of(b_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"a.xml"}, factory);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("cycle") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 8. Admissibility callback rejection fails naming the parent.
// ---------------------------------------------------------------------------
TEST_CASE("admissibility callback rejection fails naming the parent", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><port>8080</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));

    nucleus::inherit_policy policy;
    policy.admissibility = [](const nucleus::configuration_source &) -> std::string {
        return "not allowed in test";
    };
    REQUIRE(engine.set_inherit_policy(std::move(policy)));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE_FALSE(loaded);
    // The error must mention admission rejection and the rejected parent path.
    const bool has_rejected = loaded.error().find("admissibility") != std::string::npos
                              || loaded.error().find("rejected") != std::string::npos;
    REQUIRE(has_rejected);
    REQUIRE(loaded.error().find("base.xml") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 8b. Single-file load with a reject-all admissibility callback succeeds.
//     The admissibility callback must not fire on the explicitly requested
//     source -- only on parent candidates reached via inherit= declarations.
// ---------------------------------------------------------------------------
TEST_CASE("admissibility reject-all does not block a single-file load", "[chain]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));

    nucleus::inherit_policy policy;
    policy.admissibility = [](const nucleus::configuration_source &) -> std::string {
        return "reject everything";
    };
    REQUIRE(engine.set_inherit_policy(std::move(policy)));

    // Single file: no parent is ever visited, so the reject-all callback must
    // never fire and the load must succeed.
    auto loaded = engine.load(std::vector<std::string>{"only.xml"},
                              [&](const std::string &) { return xml_of(doc); });
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
}

// ---------------------------------------------------------------------------
// 8c. Two-file chain with a reject-all admissibility callback fails naming
//     the parent path, not the requested (leaf) source.
// ---------------------------------------------------------------------------
TEST_CASE("admissibility reject-all fails naming the parent in a two-file chain", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><port>8080</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));

    nucleus::inherit_policy policy;
    policy.admissibility = [](const nucleus::configuration_source &) -> std::string {
        return "reject everything";
    };
    REQUIRE(engine.set_inherit_policy(std::move(policy)));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE_FALSE(loaded);
    // The error must name the rejected parent, not "derived.xml".
    REQUIRE(loaded.error().find("base.xml") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 9. Default admit-all policy allows all parents.
// ---------------------------------------------------------------------------
TEST_CASE("default admit-all policy allows all parents", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><port>8080</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));
    // No set_inherit_policy -- default admits all parents.

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("cluster/server/port") == "8080");
}

// ---------------------------------------------------------------------------
// 10. extend=narrow obeys the default scope policy.
// ---------------------------------------------------------------------------
TEST_CASE("extend-narrow obeys default scope policy", "[chain]")
{
    // base.xml: first introduction of strain "web" (document rank = base layer rank).
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    // derived.xml: extends "web" with narrow disposition, adds protocol=tcp.
    // The derived layer rank is strictly above the base layer rank.
    // Under space_open_container_closed (default), entries at rank > Ld are excluded.
    // Ld = base layer rank; derived layer rank > Ld, so protocol=tcp is excluded.
    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="narrow"><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE(loaded);
    // Base layer entry at Ld survives.
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
    // Derived layer entry above Ld is excluded under narrow-extend + default policy.
    REQUIRE_FALSE(loaded.value().contains("cluster/server/protocol"));
}

// ---------------------------------------------------------------------------
// 11. extend=wide bypasses scope policy.
// ---------------------------------------------------------------------------
TEST_CASE("extend-wide bypasses scope policy", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    // Wide-extend: all entries for the chosen strain compose regardless of rank.
    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    REQUIRE(engine.select("web"));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE(loaded);
    // Base layer entry at Ld composes.
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
    // Derived layer entry above Ld also composes (wide-extend bypasses the filter).
    REQUIRE(loaded.value().get("cluster/server/protocol") == "tcp");
}

// ---------------------------------------------------------------------------
// 12. extend-without-base fails loudly.
// ---------------------------------------------------------------------------
TEST_CASE("extend-without-base fails loudly", "[chain]")
{
    // base.xml has no strain named "web".
    const char *base_doc = R"(
        <cluster>
            <server name="db"><port>5432</port></server>
        </cluster>)";

    // derived.xml tries to extend "web" but base has no such strain.
    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="narrow"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);
    // Select "web" so the "multiple strains, no selection" guard does not fire
    // before the extend-without-base check in Step B.
    REQUIRE(engine.select("web"));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE_FALSE(loaded);
    // Must mention "extend" and the missing base, or the strain name.
    const bool has_extend = loaded.error().find("extend") != std::string::npos
                            || loaded.error().find("base") != std::string::npos
                            || loaded.error().find("no base") != std::string::npos;
    REQUIRE(has_extend);
}

// ---------------------------------------------------------------------------
// 13. Re-open without extend disposition fails loudly.
// ---------------------------------------------------------------------------
TEST_CASE("re-open without extend disposition fails loudly", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    // derived.xml re-opens "web" without any extend= attribute.
    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web"><port>8080</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE_FALSE(loaded);
    const bool has_reopen = loaded.error().find("re-opening") != std::string::npos
                            || loaded.error().find("re-open") != std::string::npos
                            || loaded.error().find("multiple layers") != std::string::npos
                            || loaded.error().find("extend") != std::string::npos;
    REQUIRE(has_reopen);
}

// ---------------------------------------------------------------------------
// 14. Duplicate primary-key value in one document fails at pull time.
// ---------------------------------------------------------------------------
TEST_CASE("duplicate primary-key value in one document fails at pull", "[chain]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
            <server name="web"><port>443</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [&](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("duplicate") != std::string::npos);
    REQUIRE(loaded.error().find("web") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 15. Duplicate primary-key across chain layers without extend fails.
// ---------------------------------------------------------------------------
TEST_CASE("duplicate primary-key across chain layers without extend fails", "[chain]")
{
    // Same strain "web" in two chain layers without an extend disposition.
    // This is a re-open without extend -- distinct from the within-document case.
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web"><port>8080</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE_FALSE(loaded);
    // Error must mention the re-open or multiple layers.
    const bool has_error = loaded.error().find("re-opening") != std::string::npos
                           || loaded.error().find("multiple layers") != std::string::npos
                           || loaded.error().find("extend") != std::string::npos;
    REQUIRE(has_error);
}

// ---------------------------------------------------------------------------
// 16. Duplicate unique-field value across sibling instances fails.
// ---------------------------------------------------------------------------
TEST_CASE("duplicate unique-field value across sibling instances fails", "[chain]")
{
    const char *doc = R"(
        <cluster>
            <server name="web" serial="SN001"><port>80</port></server>
            <server name="db" serial="SN001"><port>5432</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    // Select "web" so the unique enforcement (Step C) runs before the
    // "multiple strains, no selection" guard would fire.
    REQUIRE(engine.select("web"));

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [&](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("unique") != std::string::npos);
    REQUIRE(loaded.error().find("SN001") != std::string::npos);
    REQUIRE(loaded.error().find("serial") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 17. Duplicate unique-field value across chain files fails.
// ---------------------------------------------------------------------------
TEST_CASE("duplicate unique-field value across chain files fails", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web" serial="SN001"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="db" serial="SN001"><port>5432</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster_with_unique(engine);
    // Select "web" so the unique enforcement (Step C) runs before the
    // "multiple strains, no selection" guard would fire.
    REQUIRE(engine.select("web"));

    auto factory = [&](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nullptr;
    };

    auto loaded = engine.load(std::vector<std::string>{"derived.xml"}, factory);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("unique") != std::string::npos);
    REQUIRE(loaded.error().find("SN001") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 18. inherit= on a non-root element fails loudly.
// ---------------------------------------------------------------------------
TEST_CASE("inherit attribute on non-root element fails loudly", "[chain]")
{
    const char *doc = R"(
        <cluster>
            <server inherit="other.xml"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [&](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("inherit") != std::string::npos);
    REQUIRE(loaded.error().find("server") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 19. Unknown extend= value is a loud parse error.
// ---------------------------------------------------------------------------
TEST_CASE("unknown extend value is a loud parse error", "[chain]")
{
    const char *doc = R"(
        <cluster>
            <server name="web" extend="diagonal"><port>80</port></server>
        </cluster>)";

    nucleus::configuration_space engine;
    declare_cluster(engine);

    auto loaded = engine.load(std::vector<std::string>{"doc.xml"},
                              [&](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("extend") != std::string::npos);
    REQUIRE(loaded.error().find("diagonal") != std::string::npos);
}
