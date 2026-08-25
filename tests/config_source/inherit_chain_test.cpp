#include "nucleus/config.h"
#include "support/builder_result_test_support.h"
#include "nucleus/config_source/inherit_declaration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include "nucleus/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>

// Inheritance chain walk, composition, extend dispositions, and duplicate
// detection tests. All tests use the factory-lambda in-memory pattern: no
// filesystem access, no host vocabulary; all shapes are generic (cluster/server).
// Selection and inherit policy are per-load parameters on load_options.

using nucleus::anchor;

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

// cluster/server keyed by "name"; leaves: port, protocol.
void declare_cluster(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server"))));
}

// Same as declare_cluster plus a unique (non-identity) "serial" field.
void declare_cluster_with_unique(nucleus::config_space_builder &engine)
{
    declare_cluster(engine);
    REQUIRE(engine.register_element(
        nucleus::unique_element("serial", anchor::keyspace("cluster/server"))));
}

// Returns the filename portion of a (possibly absolute) path string so factory
// lambdas can dispatch without knowledge of the working-directory prefix that
// weakly_canonical() prepends to relative paths when following inherit= links.
std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// Loads a document chain against `space`, carrying the per-load selection and
// inherit policy as options.
nucleus::load_result load_chain(const nucleus::config_space &space,
                                std::vector<std::string> paths,
                                std::function<nucleus::source_handle(const std::string &)> factory,
                                std::optional<std::string> selection = std::nullopt,
                                nucleus::inherit_policy policy = {})
{
    nucleus::load_options opts;
    opts.document_paths = std::move(paths);
    opts.make_document = std::move(factory);
    opts.selection = std::move(selection);
    opts.inherit = std::move(policy);
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

// Wraps an xml_source but reports a caller-supplied capability descriptor
// instead of xml_source's own, so an admissibility policy can be made to
// reject exactly the documents a test targets.
struct capped_xml_source
{
    nucleus::xml_source src;
    nucleus::capability_descriptor caps;

    nucleus::capability_descriptor capabilities() const { return caps; }
    void apply_projection(const nucleus::schema_projection &p) { src.apply_projection(p); }
    nucleus::inherit_declaration inheritance() const { return src.inheritance(); }
    nucleus::config_source_result pull() { return src.pull(); }
};

const char *const chain_a_doc = R"(<cluster><server name="web"><port>80</port></server></cluster>)";
const char *const chain_b_doc =
    R"(<cluster inherit="a.xml"><server name="web" extend="wide"><protocol>tcp</protocol></server></cluster>)";
const char *const chain_c_doc = R"(<cluster><server name="orphan"><port>1</port></server></cluster>)";
const char *const chain_d_doc =
    R"(<cluster inherit="c.xml"><server name="orphan" extend="wide"><protocol>udp</protocol></server></cluster>)";

// Wraps an xml_source and increments a shared counter on every pull(),
// forwarding capabilities()/apply_projection()/inheritance()/pull() to the
// wrapped source unchanged -- satisfies config_source plus the optional
// projects_source/inheriting_source refinements by duck typing, exactly as
// xml_source itself does.
struct counting_xml_source
{
    nucleus::xml_source  src;
    std::shared_ptr<int> pull_count;

    static nucleus::capability_descriptor capabilities()
    {
        return nucleus::xml_source::capabilities();
    }

    void apply_projection(const nucleus::schema_projection &projection)
    {
        src.apply_projection(projection);
    }

    nucleus::inherit_declaration inheritance() const { return src.inheritance(); }

    nucleus::config_source_result pull()
    {
        ++*pull_count;
        return src.pull();
    }
};

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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &) { return xml_of(base_doc); };
    auto loaded = load_chain(space, {"base.xml"}, factory, "web");
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
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

    nucleus::config_space_builder engine;
    // Schema without identity: anonymous content composes by rank.
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory);
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
    const char *base_doc = R"(
        <cluster>
            <server><port>9090</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web"><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);
    // Single named strain auto-resolves; no selection needed.

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory);
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
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
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
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
    const char *a_doc = R"(<cluster inherit="b.xml"><server name="a"><port>1</port></server></cluster>)";
    const char *b_doc = R"(<cluster inherit="c.xml"><server name="b"><port>2</port></server></cluster>)";
    const char *c_doc = R"(<cluster><server name="c"><port>3</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    nucleus::inherit_policy policy;
    policy.depth_cap = 2;

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "a.xml")
            return xml_of(a_doc);
        if(name == "b.xml")
            return xml_of(b_doc);
        if(name == "c.xml")
            return xml_of(c_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"a.xml"}, factory, std::nullopt, std::move(policy));
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("depth") != std::string::npos);
    REQUIRE(loaded.error().message.find('2') != std::string::npos);
}

// ---------------------------------------------------------------------------
// 7. Cycle detection returns a loud error naming the cycling path.
// ---------------------------------------------------------------------------
TEST_CASE("cycle in inheritance chain fails loudly naming the path", "[chain]")
{
    const char *a_doc = R"(<cluster inherit="b.xml"><server name="a"><port>1</port></server></cluster>)";
    const char *b_doc = R"(<cluster inherit="a.xml"><server name="b"><port>2</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "a.xml")
            return xml_of(a_doc);
        if(name == "b.xml")
            return xml_of(b_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"a.xml"}, factory);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("cycle") != std::string::npos);
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    nucleus::inherit_policy policy;
    policy.admissibility = [](nucleus::capability_descriptor) -> std::string {
        return "not allowed in test";
    };

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web", std::move(policy));
    REQUIRE_FALSE(loaded);
    // The error must mention admission rejection and the rejected parent path.
    const bool has_rejected = loaded.error().message.find("admissibility") != std::string::npos
                              || loaded.error().message.find("rejected") != std::string::npos;
    REQUIRE(has_rejected);
    REQUIRE(loaded.error().message.find("base.xml") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 8b. Single-file load with a reject-all admissibility callback succeeds.
// ---------------------------------------------------------------------------
TEST_CASE("admissibility reject-all does not block a single-file load", "[chain]")
{
    const char *doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    nucleus::inherit_policy policy;
    policy.admissibility = [](nucleus::capability_descriptor) -> std::string {
        return "reject everything";
    };

    // Single file: no parent is ever visited, so the reject-all callback must
    // never fire and the load must succeed.
    auto loaded = load_chain(space, {"only.xml"},
                             [&](const std::string &) { return xml_of(doc); },
                             "web", std::move(policy));
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    nucleus::inherit_policy policy;
    policy.admissibility = [](nucleus::capability_descriptor) -> std::string {
        return "reject everything";
    };

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web", std::move(policy));
    REQUIRE_FALSE(loaded);
    // The error must name the rejected parent, not "derived.xml".
    REQUIRE(loaded.error().message.find("base.xml") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 8d. A source directly requested AND reached as another request's inherited
//     parent is exempt from admissibility regardless of visit order: the
//     chain-expansion dedupe must not make the exemption depend on which role
//     (request or parent) the shared document happens to be visited under first.
// ---------------------------------------------------------------------------
TEST_CASE("directly-requested source reached as an inherited parent is exempt "
          "from admissibility regardless of request order", "[chain]")
{
    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    // Rejects any source that lacks nesting. a.xml and c.xml are both built
    // without it, so the policy would reject either if it ran against them.
    nucleus::inherit_policy policy;
    policy.admissibility = [](nucleus::capability_descriptor caps) -> std::string {
        return caps.supports(nucleus::capability::nesting) ? std::string{} : "lacks nesting";
    };

    const nucleus::capability_descriptor bare{};
    const nucleus::capability_descriptor nested{nucleus::capability::nesting};

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "a.xml")
            return nucleus::source_handle(capped_xml_source{
                nucleus::xml_source::from(nucleus::xml_source_options::of_string(chain_a_doc)), bare});
        if(name == "b.xml")
            return nucleus::source_handle(capped_xml_source{
                nucleus::xml_source::from(nucleus::xml_source_options::of_string(chain_b_doc)), nested});
        if(name == "c.xml")
            return nucleus::source_handle(capped_xml_source{
                nucleus::xml_source::from(nucleus::xml_source_options::of_string(chain_c_doc)), bare});
        if(name == "d.xml")
            return nucleus::source_handle(capped_xml_source{
                nucleus::xml_source::from(nucleus::xml_source_options::of_string(chain_d_doc)), nested});
        return nucleus::source_handle(nucleus::env_source{});
    };

    // a.xml is directly requested AND reached as b.xml's inherited parent. The
    // policy would reject it as a parent, but its direct-request membership
    // exempts it -- regardless of which order the two paths are listed in.
    auto loaded_b_first = load_chain(space, {"b.xml", "a.xml"}, factory, "web", policy);
    REQUIRE(loaded_b_first);
    REQUIRE(loaded_b_first.value().get("cluster/server/port") == "80");
    REQUIRE(loaded_b_first.value().get("cluster/server/protocol") == "tcp");

    auto loaded_a_first = load_chain(space, {"a.xml", "b.xml"}, factory, "web", policy);
    REQUIRE(loaded_a_first);
    REQUIRE(loaded_a_first.value().get("cluster/server/port") == "80");
    REQUIRE(loaded_a_first.value().get("cluster/server/protocol") == "tcp");

    // Negative control: c.xml is a genuinely transitive-only parent (never
    // directly requested), so the same policy must still reject it -- the
    // exemption above must not have disabled the admissibility check entirely.
    auto loaded_transitive_only = load_chain(space, {"d.xml"}, factory, "orphan", policy);
    REQUIRE_FALSE(loaded_transitive_only);
    const bool has_rejected =
        loaded_transitive_only.error().message.find("admissibility") != std::string::npos
        || loaded_transitive_only.error().message.find("rejected") != std::string::npos;
    REQUIRE(has_rejected);
    REQUIRE(loaded_transitive_only.error().message.find("c.xml") != std::string::npos);
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);
    // No inherit policy -- default admits all parents.

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("cluster/server/port") == "8080");
}

// ---------------------------------------------------------------------------
// 10. extend=narrow obeys the default scope policy.
// ---------------------------------------------------------------------------
TEST_CASE("extend-narrow obeys default scope policy", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="narrow"><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
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

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="wide"><protocol>tcp</protocol></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
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
    const char *base_doc = R"(
        <cluster>
            <server name="db"><port>5432</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web" extend="narrow"><port>80</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    // Select "web" so the "multiple strains, no selection" guard does not fire
    // before the extend-without-base check.
    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
    REQUIRE_FALSE(loaded);
    const bool has_extend = loaded.error().message.find("extend") != std::string::npos
                            || loaded.error().message.find("base") != std::string::npos
                            || loaded.error().message.find("no base") != std::string::npos;
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

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web"><port>8080</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory);
    REQUIRE_FALSE(loaded);
    const bool has_reopen = loaded.error().message.find("re-opening") != std::string::npos
                            || loaded.error().message.find("re-open") != std::string::npos
                            || loaded.error().message.find("multiple layers") != std::string::npos
                            || loaded.error().message.find("extend") != std::string::npos;
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_chain(space, {"doc.xml"},
                             [&](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("duplicate") != std::string::npos);
    REQUIRE(loaded.error().message.find("web") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 15. Duplicate primary-key across chain layers without extend fails.
// ---------------------------------------------------------------------------
TEST_CASE("duplicate primary-key across chain layers without extend fails", "[chain]")
{
    const char *base_doc = R"(
        <cluster>
            <server name="web"><port>80</port></server>
        </cluster>)";

    const char *derived_doc = R"(
        <cluster inherit="base.xml">
            <server name="web"><port>8080</port></server>
        </cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"derived.xml"}, factory);
    REQUIRE_FALSE(loaded);
    const bool has_error = loaded.error().message.find("re-opening") != std::string::npos
                           || loaded.error().message.find("multiple layers") != std::string::npos
                           || loaded.error().message.find("extend") != std::string::npos;
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

    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    // Select "web" so unique enforcement runs before the "multiple strains, no
    // selection" guard would fire.
    auto loaded = load_chain(space, {"doc.xml"},
                             [&](const std::string &) { return xml_of(doc); }, "web");
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("unique") != std::string::npos);
    REQUIRE(loaded.error().message.find("SN001") != std::string::npos);
    REQUIRE(loaded.error().message.find("serial") != std::string::npos);
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

    nucleus::config_space_builder engine;
    declare_cluster_with_unique(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml")
            return xml_of(base_doc);
        if(name == "derived.xml")
            return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    // Select "web" so unique enforcement runs before the "multiple strains, no
    // selection" guard would fire.
    auto loaded = load_chain(space, {"derived.xml"}, factory, "web");
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("unique") != std::string::npos);
    REQUIRE(loaded.error().message.find("SN001") != std::string::npos);
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_chain(space, {"doc.xml"},
                             [&](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("inherit") != std::string::npos);
    REQUIRE(loaded.error().message.find("server") != std::string::npos);
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

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = load_chain(space, {"doc.xml"},
                             [&](const std::string &) { return xml_of(doc); });
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("extend") != std::string::npos);
    REQUIRE(loaded.error().message.find("diagonal") != std::string::npos);
}

// Schema for the guard-axis cases below: anonymous (no primary key) so multi-file
// and multi-route chains compose freely by rank, isolating the depth/cycle/
// duplicate-canonical/re-entry guards from the keyed-instance re-open rules.
namespace {

void declare_anon_cluster(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::element("protocol", anchor::keyspace("cluster/server"))));
}

}

// ---------------------------------------------------------------------------
// 20. Depth-cap boundary: a chain EXACTLY at depth_cap loads; depth_cap+1 fails.
// ---------------------------------------------------------------------------
TEST_CASE("depth-cap boundary: exactly at the cap loads, one beyond fails", "[chain]")
{
    // a -> b -> c is a three-deep walk (depth 3). Anonymous content composes,
    // so the only thing under test is the depth guard itself.
    const char *a_doc = R"(<cluster inherit="b.xml"><server><port>1</port></server></cluster>)";
    const char *b_doc = R"(<cluster inherit="c.xml"><server><port>2</port></server></cluster>)";
    const char *c_doc = R"(<cluster><server><port>3</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_anon_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "a.xml") return xml_of(a_doc);
        if(name == "b.xml") return xml_of(b_doc);
        if(name == "c.xml") return xml_of(c_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    SECTION("exactly at the cap loads")
    {
        nucleus::inherit_policy policy;
        policy.depth_cap = 3;
        auto loaded = load_chain(space, {"a.xml"}, factory, std::nullopt, std::move(policy));
        REQUIRE(loaded);
        // a is the requested file (highest rank); its port wins the contest.
        REQUIRE(loaded.value().get("cluster/server/port") == "1");
    }

    SECTION("one beyond the cap fails naming depth and the limit")
    {
        nucleus::inherit_policy policy;
        policy.depth_cap = 2;
        auto loaded = load_chain(space, {"a.xml"}, factory, std::nullopt, std::move(policy));
        REQUIRE_FALSE(loaded);
        REQUIRE(loaded.error().message.find("depth") != std::string::npos);
        REQUIRE(loaded.error().message.find('2') != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// 21. Three-file cycle (a -> b -> c -> a) fails loudly naming a path on the cycle.
// ---------------------------------------------------------------------------
TEST_CASE("three-file cycle fails loudly naming a path on the cycle", "[chain]")
{
    const char *a_doc = R"(<cluster inherit="b.xml"><server><port>1</port></server></cluster>)";
    const char *b_doc = R"(<cluster inherit="c.xml"><server><port>2</port></server></cluster>)";
    const char *c_doc = R"(<cluster inherit="a.xml"><server><port>3</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_anon_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "a.xml") return xml_of(a_doc);
        if(name == "b.xml") return xml_of(b_doc);
        if(name == "c.xml") return xml_of(c_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"a.xml"}, factory);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("cycle") != std::string::npos);
    // The reported path names a file on the cycle (a.xml is re-entered).
    REQUIRE(loaded.error().message.find("a.xml") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 22. Duplicate-canonical path: the SAME file reached via two routes resolves
//     deterministically (NOT a cycle error). The walker releases a path from the
//     visited set on return, so requesting base directly AND pulling it through a
//     derived file's inherit= is a duplicate pull, not a cycle.
// ---------------------------------------------------------------------------
TEST_CASE("duplicate-canonical path reached two ways resolves deterministically",
          "[chain]")
{
    const char *base_doc = R"(<cluster><server><port>80</port></server></cluster>)";
    const char *derived_doc =
        R"(<cluster inherit="base.xml"><server><protocol>tcp</protocol></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_anon_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml") return xml_of(base_doc);
        if(name == "derived.xml") return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    // Request base directly AND derived (which inherits base): base is canonically
    // pulled twice via different routes. This must NOT be flagged as a cycle.
    auto loaded = load_chain(space, {"base.xml", "derived.xml"}, factory);
    REQUIRE(loaded);
    // Deterministic composition: base supplies port, derived supplies protocol.
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
    REQUIRE(loaded.value().get("cluster/server/protocol") == "tcp");
}

// ---------------------------------------------------------------------------
// Duplicate-canonical path, named strain: base.xml is requested directly AND
// reached again as derived.xml's inherited parent, on a schema with a primary
// key. derived.xml introduces its own, DIFFERENT named strain ("app") rather
// than re-declaring "web", so a correct load never needs an extend attribute:
// "web" is written exactly once. Before expand()'s dedupe was scoped across
// the whole call, base.xml was walked twice -- once as the directly-requested
// path, once again as derived.xml's recursively-expanded parent -- so slice()
// saw "web" introduced at two layers with no extend= disposition and rejected
// the load as an illegitimate re-open, even though the user wrote it once.
// ---------------------------------------------------------------------------
TEST_CASE("duplicate-canonical path reached two ways resolves deterministically "
          "for a named strain", "[chain]")
{
    const char *base_doc =
        R"(<cluster><server name="web"><port>80</port></server></cluster>)";
    const char *derived_doc =
        R"(<cluster inherit="base.xml"><server name="app"><port>443</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml") return xml_of(base_doc);
        if(name == "derived.xml") return xml_of(derived_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    auto loaded = load_chain(space, {"base.xml", "derived.xml"}, factory, "web");
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
}

// ---------------------------------------------------------------------------
// Each chain document is pulled exactly once per load, proven directly by an
// instrumented counting source rather than by the absence of a re-open error.
// Before the walk-pull's batch was cached on chain_entry, every chain document
// was pulled twice per load: once by chain_walker to read its inherit=
// declaration, once again by fold() to consume its entries -- for every
// document, not only ones reached twice by different routes.
// ---------------------------------------------------------------------------
TEST_CASE("each chain document is pulled exactly once per load", "[chain]")
{
    const char *base_doc =
        R"(<cluster><server name="web"><port>80</port></server></cluster>)";
    const char *derived_doc =
        R"(<cluster inherit="base.xml">)"
        R"(<server name="web" extend="wide"><protocol>tcp</protocol></server></cluster>)";

    std::map<std::string, std::shared_ptr<int>> counters;

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);

        const char *text = nullptr;
        if(name == "base.xml") text = base_doc;
        else if(name == "derived.xml") text = derived_doc;
        else return nucleus::source_handle(nucleus::env_source{});

        std::shared_ptr<int> &counter = counters[name];
        if(!counter)
            counter = std::make_shared<int>(0);
        return nucleus::source_handle(counting_xml_source{
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)),
            counter});
    };

    // Single top-level request: base.xml is only reached via the inherit chain.
    auto loaded = load_chain(space, {"derived.xml"}, factory);
    REQUIRE(loaded);
    REQUIRE(*counters.at("base.xml") == 1);
    REQUIRE(*counters.at("derived.xml") == 1);
}

// ---------------------------------------------------------------------------
// 23. Function-chain re-entry: a second independent requested path re-enters the
//     walk; the visited set is released between top-level walks (RAII path_guard),
//     so the second path is NOT falsely flagged as a cycle.
// ---------------------------------------------------------------------------
TEST_CASE("independent second requested path is not falsely flagged as a cycle",
          "[chain]")
{
    const char *first_doc = R"(<cluster><server><port>80</port></server></cluster>)";
    const char *second_doc = R"(<cluster><server><protocol>tcp</protocol></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_anon_cluster(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "first.xml") return xml_of(first_doc);
        if(name == "second.xml") return xml_of(second_doc);
        return nucleus::source_handle(nucleus::env_source{});
    };

    // Two independent top-level walks in one expand(): the first walk's visited
    // entries must be released before the second begins, or the second would
    // spuriously trip the cycle guard.
    auto loaded = load_chain(space, {"first.xml", "second.xml"}, factory);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
    REQUIRE(loaded.value().get("cluster/server/protocol") == "tcp");
}
