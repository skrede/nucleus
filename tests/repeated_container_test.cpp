// Unified fold: repeated containers and repeated leaves stored as indexed scalars
// in m_values; wholesale-replace across layers; extend= guard; get_all numeric order.
// schema_enforcer normalizes indexed paths via canonical_text.
// get_as() loud error for unindexed crossing; get_all_as() typed gather.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converters.h"

#include "nucleus/config.h"
#include "nucleus/error.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

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

// Schema: cluster -> node (repeated container) -> port (leaf)
void declare_cluster_node_schema(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::element("port", anchor::keyspace("cluster/node"))));
}

// Schema: config -> tags (repeated leaf)
void declare_tags_schema(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("tags", anchor::keyspace("config"))));
}

}

TEST_CASE("unified fold -- two-node indexed scalars stored in m_values",
          "[repeated_container][fold]")
{
    nucleus::config_space_builder engine;
    declare_cluster_node_schema(engine);
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // Indexed scalars stored in m_values; accessible via indexed paths.
    REQUIRE(cfg.get("cluster/node[0]/port") == "80");
    REQUIRE(cfg.get("cluster/node[1]/port") == "90");
    // Non-indexed container path returns nothing.
    REQUIRE_FALSE(cfg.get("cluster/node/port").has_value());
}

TEST_CASE("unified fold -- repeated leaf stored as indexed scalars",
          "[repeated_container][fold][leaf]")
{
    nucleus::config_space_builder engine;
    declare_tags_schema(engine);
    nucleus::config_space space = engine.build();

    auto src = xml_of("<config><tags>a</tags><tags>b</tags></config>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // Repeated leaves stored as indexed scalars.
    REQUIRE(cfg.get("config/tags[0]") == "a");
    REQUIRE(cfg.get("config/tags[1]") == "b");
    // Plain path is absent (no scalar at unindexed path).
    REQUIRE_FALSE(cfg.get("config/tags").has_value());
}

TEST_CASE("wholesale-replace -- a two-node layer over a three-node base replaces the "
          "two instances it addresses and leaves the third in place",
          "[repeated_container][wholesale_replace]")
{
    nucleus::config_space_builder engine;
    declare_cluster_node_schema(engine);
    nucleus::config_space space = engine.build();

    auto src1 = xml_of(
        "<cluster>"
        "<node><port>10</port></node>"
        "<node><port>20</port></node>"
        "<node><port>30</port></node>"
        "</cluster>");
    auto src2 = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");

    // src1 at lower precedence, src2 at higher precedence.
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src1), std::move(src2)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    REQUIRE(cfg.get("cluster/node[0]/port") == "80");
    REQUIRE(cfg.get("cluster/node[1]/port") == "90");
    REQUIRE(cfg.get("cluster/node[2]/port") == "30");
}

TEST_CASE("wholesale-replace -- nested repeated-in-repeated sweep operates at "
          "the innermost container, not the outer one",
          "[repeated_container][wholesale_replace][nested]")
{
    // Schema: cluster -> node (repeated) -> label (leaf), tags (repeated) -> name.
    // "node" and "node/tags" are BOTH declared repeated containers, one nested
    // inside the other; "cluster/node" is a prefix of "cluster/node/tags" so a
    // container-prefix selection that stops at the first (shortest) match picks
    // the wrong, outer container for any entry actually belonging to "tags".
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::element("label", anchor::keyspace("cluster/node"))));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("tags", anchor::keyspace("cluster/node"))));
    REQUIRE(engine.register_element(
        nucleus::element("name", anchor::keyspace("cluster/node/tags"))));
    nucleus::config_space space = engine.build();

    // Layer 1 supplies both label and a tag under node[0].
    auto src1 = xml_of(
        "<cluster>"
        "<node><label>keep</label><tags><name>a</name></tags></node>"
        "</cluster>");
    // Layer 2 (higher precedence) touches ONLY the nested "tags" container --
    // it never mentions "label" at all.
    auto src2 = xml_of(
        "<cluster>"
        "<node><tags><name>z</name></tags></node>"
        "</cluster>");

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src1), std::move(src2)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // A too-shallow sweep (at "cluster/node" instead of "cluster/node/tags")
    // would wipe "label" as collateral damage even though layer 2 never
    // touched it; the innermost sweep must leave it untouched.
    REQUIRE(cfg.get("cluster/node[0]/label") == "keep");
    // The nested "tags" container itself must still wholesale-replace: layer
    // 2's single tag replaces layer 1's, not merges alongside it.
    REQUIRE(cfg.get("cluster/node[0]/tags[0]/name") == "z");
    REQUIRE_FALSE(cfg.get("cluster/node[0]/tags[1]/name").has_value());
}

TEST_CASE("extend= on repeated container returns layering_violation",
          "[repeated_container][extend_guard]")
{
    nucleus::config_space_builder engine;
    declare_cluster_node_schema(engine);
    nucleus::config_space space = engine.build();

    // An extend= attribute on a repeated container instance is not supported.
    auto src1 = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "</cluster>");
    // A second XML document with extend="narrow" on a node element.
    // The xml_source produces an extend_disposition with container_path="cluster/node".
    auto src2 = xml_of(
        "<cluster>"
        "<node extend=\"narrow\"><port>90</port></node>"
        "</cluster>");

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src1), std::move(src2)},
        {});
    REQUIRE_FALSE(loaded);
    const bool is_layering_violation =
        loaded.error().code == nucleus::errc::layering_violation;
    REQUIRE(is_layering_violation);
}

TEST_CASE("get_all gather -- cluster/node/port across indexed instances",
          "[repeated_container][get_all]")
{
    nucleus::config_space_builder engine;
    declare_cluster_node_schema(engine);
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // get_all gathers across indexed instances in numeric ordinal order.
    auto ports = cfg.get_all("cluster/node/port");
    REQUIRE(ports.size() == 2);
    REQUIRE(ports[0] == "80");
    REQUIRE(ports[1] == "90");
}

TEST_CASE("get_all gather -- repeated leaf in numeric ordinal order",
          "[repeated_container][get_all][leaf]")
{
    nucleus::config_space_builder engine;
    declare_tags_schema(engine);
    nucleus::config_space space = engine.build();

    auto src = xml_of("<config><tags>a</tags><tags>b</tags></config>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    auto tags = cfg.get_all("config/tags");
    REQUIRE(tags.size() == 2);
    REQUIRE(tags[0] == "a");
    REQUIRE(tags[1] == "b");
}

TEST_CASE("get_all gather -- numeric ordinal order with N >= 11 instances",
          "[repeated_container][get_all][ordering]")
{
    // Build a space with 12 node instances via runtime_source to avoid
    // a large XML document. runtime_source declares duplicate_keys.
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::element("port", anchor::keyspace("cluster/node"))));
    nucleus::config_space space = engine.build();

    // Inject 12 indexed scalar entries directly via runtime_source.
    // Paths are cluster/node[0]/port through cluster/node[11]/port.
    nucleus::runtime_source src;
    for(int i = 0; i < 12; ++i)
        src.set("cluster/node[" + std::to_string(i) + "]/port", std::to_string(i * 10));

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    auto ports = cfg.get_all("cluster/node/port");
    REQUIRE(ports.size() == 12);

    // Must be in numeric order 0..11, NOT lexicographic (which would put 10, 11 before 2).
    for(int i = 0; i < 12; ++i)
        REQUIRE(ports[static_cast<std::size_t>(i)] == std::to_string(i * 10));
}

// ---------------------------------------------------------------------------
// schema_enforcer canonical_text normalization
// ---------------------------------------------------------------------------

namespace {

nucleus::key_path kp(const char *text) { return nucleus::key_path::parse(text).value(); }

// Builds a schema_registry with cluster -> node (repeated container) -> port.
nucleus::schema_registry cluster_node_registry()
{
    nucleus::schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace("cluster/node"))));
    return reg;
}

}

TEST_CASE("schema_enforcer: indexed path normalizes to declared path",
          "[repeated_container][enforcer]")
{
    const auto reg = cluster_node_registry();

    // Keyspace with indexed paths -- these are the post-fold form.
    nucleus::keyspace ks;
    ks.set(kp("cluster/node[0]/port"), nucleus::value::owned("80"));
    ks.set(kp("cluster/node[1]/port"), nucleus::value::owned("90"));

    // Both indexed paths should pass the unknown-path gate via canonical_text
    // normalization (cluster/node[0]/port -> cluster/node/port which is declared).
    const auto result = nucleus::schema_enforcer::validate(reg, ks);
    REQUIRE(result);
}

TEST_CASE("schema_enforcer: unknown indexed path produces violation",
          "[repeated_container][enforcer]")
{
    const auto reg = cluster_node_registry();

    // An indexed path whose canonical form is NOT declared.
    nucleus::keyspace ks;
    ks.set(kp("cluster/node[0]/unknown_field"), nucleus::value::owned("x"));

    const auto result = nucleus::schema_enforcer::validate(reg, ks);
    REQUIRE_FALSE(result);
    const bool mentions_not_declared = std::any_of(
        result.error().begin(), result.error().end(),
        [](const nucleus::schema_violation &v)
        { return v.reason.find("not declared") != std::string::npos; });
    REQUIRE(mentions_not_declared);
}

TEST_CASE("schema_enforcer: required check satisfied by indexed instance",
          "[repeated_container][enforcer][required]")
{
    nucleus::schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    // port is required
    nucleus::schema_element port_el =
        nucleus::required_element("port", anchor::keyspace("cluster/node"));
    port_el.repeated = false;
    REQUIRE(reg.attach(std::move(port_el)));

    // One indexed instance present: cluster/node[0]/port -- satisfies required.
    nucleus::keyspace ks_with;
    ks_with.set(kp("cluster/node[0]/port"), nucleus::value::owned("80"));
    REQUIRE(nucleus::schema_enforcer::validate(reg, ks_with));

    // No instances present: cluster/node/port required but absent -- violation.
    nucleus::keyspace ks_without;
    const auto result = nucleus::schema_enforcer::validate(reg, ks_without);
    REQUIRE_FALSE(result);
    const bool mentions_required = std::any_of(
        result.error().begin(), result.error().end(),
        [](const nucleus::schema_violation &v)
        { return v.reason.find("required field") != std::string::npos; });
    REQUIRE(mentions_required);
}

// ---------------------------------------------------------------------------
// get_as() loud error; get() nullopt; get_all_as() typed gather
// ---------------------------------------------------------------------------

TEST_CASE("get_as on unindexed repeated container path returns errc::index_required",
          "[repeated_container][get_as]")
{
    nucleus::config_space_builder engine;
    declare_cluster_node_schema(engine);
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // Unindexed path crossing the repeated container must return index_required.
    auto result = cfg.get_as<std::string>("cluster/node");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == nucleus::errc::index_required);
    REQUIRE(result.error().message.find("cluster/node") != std::string::npos);
    REQUIRE(result.error().message.find('2') != std::string::npos);

    // Indexed path must succeed (port is typed only if converter registered,
    // otherwise absent_key for untyped; but cluster/node[0]/port IS present).
    REQUIRE(cfg.get("cluster/node[0]/port") == "80");
}

TEST_CASE("get on unindexed repeated container path returns nullopt",
          "[repeated_container][get]")
{
    nucleus::config_space_builder engine;
    declare_cluster_node_schema(engine);
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // get() on unindexed path that crosses repeated container -> nullopt (legacy surface).
    REQUIRE(cfg.get("cluster/node") == std::nullopt);
    // Indexed path works normally.
    REQUIRE(cfg.get("cluster/node[0]/port") == "80");
    REQUIRE(cfg.get("cluster/node[1]/port") == "90");
}

TEST_CASE("get_all_as gathers typed values across indexed instances",
          "[repeated_container][get_all_as]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    // Register a typed double element for port.
    nucleus::schema_element port_el =
        nucleus::typed_element<double>("port", anchor::keyspace("cluster/node"));
    REQUIRE(engine.register_element(std::move(port_el)));
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    auto result = cfg.get_all_as<double>("cluster/node/port");
    REQUIRE(result.has_value());
    REQUIRE(*result == std::vector<double>{80.0, 90.0});
}

// ---------------------------------------------------------------------------
// Repeated is orthogonal to identity/unique
// ---------------------------------------------------------------------------

TEST_CASE("Repeated element with no identity or unique attaches and loads",
          "[repeated_container]")
{
    SECTION("repeated container with no identity/unique succeeds")
    {
        // A repeated element with no identity/unique declaration attaches successfully.
        nucleus::config_space_builder engine;
        REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
        REQUIRE(engine.register_element(
            nucleus::repeated_element("node", anchor::keyspace("cluster"))));
        REQUIRE(engine.register_element(
            nucleus::element("port", anchor::keyspace("cluster/node"))));
        nucleus::config_space space = engine.build();

        auto src = xml_of("<cluster><node><port>80</port></node></cluster>");
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{std::move(src)}, {});
        REQUIRE(loaded);
        REQUIRE(loaded.value().get("cluster/node[0]/port") == "80");
    }

    SECTION("repeated element coexists with primary key in a different container")
    {
        // A repeated element in one container and an identity element in a
        // DIFFERENT container coexist without conflict.
        nucleus::config_space_builder engine;
        // Container A: cluster/node (repeated)
        REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
        REQUIRE(engine.register_element(
            nucleus::repeated_element("node", anchor::keyspace("cluster"))));
        REQUIRE(engine.register_element(
            nucleus::element("port", anchor::keyspace("cluster/node"))));
        // Container B: registry/server (keyed by name) -- primary key, different container.
        REQUIRE(engine.register_element(nucleus::element("registry", anchor::root())));
        REQUIRE(engine.register_element(
            nucleus::element("server", anchor::keyspace("registry"))));
        REQUIRE(engine.register_element(
            nucleus::primary_key_element("name", anchor::keyspace("registry/server"))));
        // Both registrations must succeed: no conflict between the two containers.
        nucleus::config_space space = engine.build();

        auto src = xml_of(
            "<cluster><node><port>80</port></node></cluster>");
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{std::move(src)}, {});
        REQUIRE(loaded);
        REQUIRE(loaded.value().get("cluster/node[0]/port") == "80");
    }

    SECTION("repeated + unique combination is rejected at attach")
    {
        // unique requires a single comparable value; that is incompatible with
        // repeated (a collection). The registry must reject this at attach time.
        nucleus::schema_registry reg;
        REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
        nucleus::schema_element el = nucleus::repeated_element("tag", anchor::keyspace("cluster"));
        el.unique = true;
        auto result = reg.attach(std::move(el));
        REQUIRE_FALSE(result);
        REQUIRE(result.error().find("unique") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Repeated container inside a keyed container -- ordinals survive slice
// ---------------------------------------------------------------------------

TEST_CASE("Repeated container inside keyed container -- ordinals survive slice",
          "[repeated_container]")
{
    // Schema: cluster -> server (keyed by name) -> route (repeated) -> port.
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("route", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::element("port", anchor::keyspace("cluster/server/route"))));
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<server name=\"primary\">"
        "<route><port>80</port></route>"
        "<route><port>443</port></route>"
        "</server>"
        "</cluster>");

    nucleus::load_options opts;
    opts.selection = "primary";
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)}, opts);
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // Ordinal segments must survive slice(): the key "primary" is stripped but
    // route[0] and route[1] remain at their declared positions.
    REQUIRE(cfg.get("cluster/server/route[0]/port") == "80");
    REQUIRE(cfg.get("cluster/server/route[1]/port") == "443");

    // Non-selected strain data must be absent.
    REQUIRE_FALSE(cfg.contains("cluster/server/primary/route"));
}

TEST_CASE("Repeated container inside keyed container -- later layer's new strain "
          "does not delete an earlier strain's route",
          "[repeated_container]")
{
    // Same schema as above: cluster -> server (keyed by name) -> route
    // (repeated) -> port. base.xml declares strain "a"; derived.xml inherits
    // from it and introduces a DIFFERENT strain "b" with its own route -- a
    // legal, brand-new strain, not an extend= of "a".
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("route", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(
        nucleus::element("port", anchor::keyspace("cluster/server/route"))));
    nucleus::config_space space = engine.build();

    const char *base_doc =
        "<cluster>"
        "<server name=\"a\"><route><port>80</port></route></server>"
        "</cluster>";
    const char *derived_doc =
        "<cluster inherit=\"base.xml\">"
        "<server name=\"b\"><route><port>443</port></route></server>"
        "</cluster>";

    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document = [&](const std::string &path) -> nucleus::source_handle {
        if(path == "base.xml")
            return nucleus::source_handle(xml_of(base_doc));
        return nucleus::source_handle(xml_of(derived_doc));
    };

    // Selecting the EARLIER strain ("a") must not lose its route to the
    // later layer's introduction of a different strain's route.
    opts.selection = "a";
    auto loaded_a = nucleus::load_config(space, nucleus::source_stack{}, opts);
    REQUIRE(loaded_a);
    REQUIRE(loaded_a.value().get("cluster/server/route[0]/port") == "80");

    // Selecting the later strain ("b") sees its own route and no leakage
    // from strain "a".
    opts.selection = "b";
    auto loaded_b = nucleus::load_config(space, nucleus::source_stack{}, opts);
    REQUIRE(loaded_b);
    REQUIRE(loaded_b.value().get("cluster/server/route[0]/port") == "443");
    REQUIRE_FALSE(loaded_b.value().contains("cluster/server/a/route"));
}

// ---------------------------------------------------------------------------
// extend= targeting repeated container via inheritance chain
// ---------------------------------------------------------------------------

TEST_CASE("extend= on repeated container via document inheritance is a layering violation",
          "[repeated_container]")
{
    // Two XML documents in an inheritance chain: base declares cluster/node
    // instances; the derived document applies extend= to a node element inside
    // the repeated container. The fold must return layering_violation.
    nucleus::config_space_builder engine;
    declare_cluster_node_schema(engine);
    nucleus::config_space space = engine.build();

    const char *base_doc =
        "<cluster>"
        "<node><port>80</port></node>"
        "</cluster>";
    const char *derived_doc =
        "<cluster>"
        "<node extend=\"narrow\"><port>90</port></node>"
        "</cluster>";

    nucleus::load_options opts;
    opts.document_paths = {"base.xml", "derived.xml"};
    opts.make_document = [&](const std::string &path) -> nucleus::source_handle {
        if(path == "base.xml")
            return nucleus::source_handle(xml_of(base_doc));
        return nucleus::source_handle(xml_of(derived_doc));
    };

    auto loaded = nucleus::load_config(space, nucleus::source_stack{}, opts);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
    // Message must name the offending repeated container path.
    REQUIRE(loaded.error().message.find("cluster/node") != std::string::npos);
}

// ---------------------------------------------------------------------------
// get_all gathers across instances with floating-point values
// ---------------------------------------------------------------------------

TEST_CASE("get_all gathers across three repeated instances in ordinal order",
          "[repeated_container][gather]")
{
    // Three-node schema; XML with port values 1.5, 2.0, 3.0 (as text scalars).
    nucleus::config_space_builder engine;
    declare_cluster_node_schema(engine);
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<node><port>1.5</port></node>"
        "<node><port>2.0</port></node>"
        "<node><port>3.0</port></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)}, {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // get_all returns values in ordinal order.
    auto ports = cfg.get_all("cluster/node/port");
    REQUIRE(ports == std::vector<std::string>{"1.5", "2.0", "3.0"});

    // get() on the container path (no scalar at unindexed crossing) returns nullopt.
    REQUIRE(cfg.get("cluster/node") == std::nullopt);
}

TEST_CASE("get_all_as gathers typed double values across three instances",
          "[repeated_container][gather][typed]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    nucleus::schema_element port_el =
        nucleus::typed_element<double>("port", anchor::keyspace("cluster/node"));
    REQUIRE(engine.register_element(std::move(port_el)));
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<node><port>1.5</port></node>"
        "<node><port>2.0</port></node>"
        "<node><port>3.0</port></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)}, {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    auto result = cfg.get_all_as<double>("cluster/node/port");
    REQUIRE(result.has_value());
    REQUIRE(*result == std::vector<double>{1.5, 2.0, 3.0});
}

// ---------------------------------------------------------------------------
// index_required error message reports distinct instance count, not entry count
// ---------------------------------------------------------------------------

TEST_CASE("get_as index_required reports instance count not entry count",
          "[repeated_container][WR01][get_as]")
{
    // Schema: cluster -> node (repeated) -> port + weight (two fields per instance).
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::element("port",   anchor::keyspace("cluster/node"))));
    REQUIRE(engine.register_element(
        nucleus::element("weight", anchor::keyspace("cluster/node"))));
    nucleus::config_space space = engine.build();

    // Two instances, each with two fields -> 4 indexed entries in m_values.
    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port><weight>1</weight></node>"
        "<node><port>90</port><weight>2</weight></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)}, {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // get_as on the container path must report 2 instances, not 4 entries.
    auto result = cfg.get_as<std::string>("cluster/node");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == nucleus::errc::index_required);
    // The message must say "2 instance(s)", not "4 instance(s)".
    REQUIRE(result.error().message.find("2 instance") != std::string::npos);
}

TEST_CASE("A flat scalar under a repeated container is rejected at the source boundary, "
          "naming the source and the path",
          "[repeated_container][convert]")
{
    // Schema: cluster -> node (repeated) -> port (typed, so convert() visits it).
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    nucleus::schema_element port_el =
        nucleus::typed_element<int>("port", anchor::keyspace("cluster/node"));
    REQUIRE(engine.register_element(std::move(port_el)));
    nucleus::config_space space = engine.build();

    // Two properly-indexed instances from a document source...
    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    // ...plus a value written directly at the plain declared path, which names no
    // instance of the repeated container and so is unaddressable by construction.
    nucleus::runtime_source malformed;
    malformed.set("cluster/node/port", "99");

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src), std::move(malformed)}, {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::malformed_source);
    REQUIRE(loaded.error().message.find("stack[1]") != std::string::npos);
    REQUIRE(loaded.error().message.find("cluster/node/port") != std::string::npos);
    REQUIRE(loaded.error().message.find("cluster/node") != std::string::npos);
}

TEST_CASE("Indexed-only instances under a repeated container convert without error",
          "[repeated_container][convert]")
{
    // The legitimate, overwhelmingly common shape: no coexisting plain scalar.
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    nucleus::schema_element port_el =
        nucleus::typed_element<int>("port", anchor::keyspace("cluster/node"));
    REQUIRE(engine.register_element(std::move(port_el)));
    nucleus::config_space space = engine.build();

    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(src)}, {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get_as<int>("cluster/node[0]/port").value() == 80);
    REQUIRE(loaded.value().get_as<int>("cluster/node[1]/port").value() == 90);
}
