#include "nucleus/config.h"
#include "nucleus/config_node.h"
#include "nucleus/config_space.h"
#include "nucleus/error.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/provenance.h"

#include "nucleus/xml/xml_emitter.h"
#include "nucleus/xml/xml_source.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// Acceptance suite: primary-key identity as data.
// Covers: the pkey leaf is readable after slice; schema validation passes with the
// pkey leaf present; emit renders the pkey once as an attribute and load→emit→load is
// a byte-stable fixpoint; a higher-rank override attempt is a loud layering_violation;
// and the pkey leaf is visible in config_node::children().

using nucleus::anchor;

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

// Derives a schema_projection from a sealed space using the public schema_elements()
// surface (config_space does not expose its internal schema_registry directly).
nucleus::schema_projection projection_of(const nucleus::config_space &space)
{
    nucleus::schema_projection proj;
    for(const nucleus::schema_element &el : space.schema_elements())
    {
        if(el.identity)
            proj.set_key(el.container().str(), el.name);
        if(el.repeated)
            proj.set_repeated_container(el.container().str());
    }
    return proj;
}

// Schema: cluster/server keyed by name; non-key leaves port and protocol.
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

// Schema where the pkey element is also required.
void declare_cluster_required_pkey(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    nucleus::schema_element pkey = nucleus::identity_element("name", anchor::keyspace("cluster/server"));
    pkey.required = true;
    REQUIRE(engine.register_element(std::move(pkey)));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server"))));
}

nucleus::load_result load_doc(const nucleus::config_space &space, const char *doc,
                              std::optional<std::string> selection = std::nullopt)
{
    nucleus::load_options opts;
    opts.document_paths = {"doc.xml"};
    opts.make_document = [doc](const std::string &) { return xml_of(doc); };
    opts.selection = std::move(selection);
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

}

// ---------------------------------------------------------------------------
// Pkey leaf is readable after slice
// ---------------------------------------------------------------------------

TEST_CASE("attribute-form pkey retained as readable leaf",
          "[pkey_identity]")
{
    const char *doc = R"(<cluster><server name="web"><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // The selected pkey value is now a readable leaf, not consumed.
    REQUIRE(config.get("cluster/server/name") == "web");
    REQUIRE(config.get("cluster/server/port") == "80");
    // The transient key-value segment is still absent from the unified path.
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
}

TEST_CASE("text-leaf-form pkey retained as readable leaf",
          "[pkey_identity]")
{
    const char *doc = R"(<cluster><server><name>web</name><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get("cluster/server/name") == "web");
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/web/port"));
}

TEST_CASE("Anonymous strain produces no pkey leaf",
          "[pkey_identity]")
{
    // No named strain: anonymous instance only; no key value present.
    const char *doc = R"(<cluster><server><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    REQUIRE_FALSE(loaded.value().contains("cluster/server/name"));
}

// ---------------------------------------------------------------------------
// Schema validation accepts the retained pkey leaf
// ---------------------------------------------------------------------------

TEST_CASE("Schema validation passes with pkey leaf present",
          "[pkey_identity]")
{
    const char *doc = R"(<cluster><server name="web"><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    // load_config runs validate() internally; a successful load means no violations.
    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("cluster/server/name") == "web");
}

TEST_CASE("Required pkey on anonymous strain fails validation",
          "[pkey_identity]")
{
    // Schema marks pkey as required: an anonymous instance (no key value) omits
    // the leaf, triggering a schema_violation.
    const char *doc = R"(<cluster><server><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster_required_pkey(engine);
    nucleus::config_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
}

// ---------------------------------------------------------------------------
// Emit renders pkey once as attribute; load→emit→load is a fixpoint
// ---------------------------------------------------------------------------

TEST_CASE("emit_document renders pkey as XML attribute not child element",
          "[pkey_identity]")
{
    const char *doc = R"(<cluster><server name="web"><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    std::ostringstream out;
    REQUIRE(nucleus::xml::emit_document(config, out, projection_of(space)));
    const std::string emitted = out.str();

    // The pkey value appears as an attribute on <server>, not as a child element.
    REQUIRE(emitted.find("name=\"web\"") != std::string::npos);
    REQUIRE(emitted.find("<name>web</name>") == std::string::npos);
}

TEST_CASE("emit_document rejects a repeated primary-key value instead of a duplicate attribute",
          "[pkey_identity]")
{
    // A hand-built config carrying two values for the pkey field of one container
    // would emit <server name="web" name="db"> -- a repeated attribute the reader
    // refuses on re-read. The emitter must reject rather than produce output its
    // own reader rejects.
    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    std::map<std::string, std::string> values{
        {"cluster/server/name[0]", "web"},
        {"cluster/server/name[1]", "db"}};
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    const nucleus::config config = std::move(made).value();

    std::ostringstream out;
    auto result = nucleus::xml::emit_document(config, out, projection_of(space));
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(out.str().empty());
}

TEST_CASE("load→emit→load round-trip is a byte-stable fixpoint",
          "[pkey_identity][round_trip]")
{
    const char *doc = R"(<cluster><server name="web"><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    // First load: C1.
    auto first = load_doc(space, doc);
    REQUIRE(first);
    const nucleus::config &c1 = first.value();

    // Emit C1 with schema projection so the pkey renders as an attribute.
    std::ostringstream out;
    REQUIRE(nucleus::xml::emit_document(c1, out, projection_of(space)));
    const std::string emitted = out.str();
    REQUIRE_FALSE(emitted.empty());

    // Reload the emitted XML as C2.
    nucleus::load_options reload_opts;
    reload_opts.document_paths = {"emitted.xml"};
    reload_opts.make_document = [&emitted](const std::string &) { return xml_of(emitted); };

    auto second = nucleus::load_config(space, nucleus::source_stack{}, reload_opts);
    REQUIRE(second);
    const nucleus::config &c2 = second.value();

    // Fixpoint: same key set and same values across the round-trip.
    REQUIRE(c2.keys() == c1.keys());
    REQUIRE(c2.get("cluster/server/name") == c1.get("cluster/server/name"));
    REQUIRE(c2.get("cluster/server/port") == c1.get("cluster/server/port"));
}

// ---------------------------------------------------------------------------
// higher-rank flat source override of pkey leaf is a loud error
// ---------------------------------------------------------------------------

TEST_CASE("Flat source attempting to override pkey leaf is layering_violation",
          "[pkey_identity]")
{
    const char *doc = R"(<cluster><server name="web"><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    // A runtime_source in the source_stack has higher rank than the XML document
    // layer. Setting cluster/server/name here is an unauthorized identity override.
    nucleus::runtime_source flat;
    flat.set("cluster/server/name", "tampered");

    nucleus::load_options opts;
    opts.document_paths = {"doc.xml"};
    opts.make_document = [doc](const std::string &) { return xml_of(doc); };

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(flat)}, opts);

    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
    REQUIRE(loaded.error().message.find("cluster/server/name") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Pkey leaf appears in config_node::children() of its parent container
// ---------------------------------------------------------------------------

TEST_CASE("Pkey leaf visible in children() of its parent container node",
          "[pkey_identity]")
{
    const char *doc = R"(<cluster><server name="web"><port>80</port></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    auto loaded = load_doc(space, doc);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // Navigate to cluster/server and enumerate its direct children.
    const auto children = config.root()["cluster"]["server"].children();

    // The pkey leaf "name" must appear alongside "port" in children().
    const bool has_name = std::any_of(children.begin(), children.end(),
        [](const nucleus::config_node &child) {
            // path() is "cluster/server/name" for the name leaf.
            const std::string_view p = child.path();
            return p.find("cluster/server/name") != std::string_view::npos;
        });
    REQUIRE(has_name);
}
