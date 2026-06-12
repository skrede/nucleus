// Unified fold: repeated containers and repeated leaves stored as indexed scalars
// in m_values; wholesale-replace across layers; extend= guard; get_all numeric order.
// D-08: schema_enforcer normalizes indexed paths via canonical_text.
// D-21: get_as() loud error for unindexed crossing; get_all_as() typed gather.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converters.h"

#include "nucleus/config.h"
#include "nucleus/error.h"
#include "nucleus/capability.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/config_source/config_source.h"

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

TEST_CASE("wholesale-replace -- layer 2 with 2 nodes replaces layer 1 three-node layer",
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

    // Higher-rank layer wins with 2 nodes; first layer's 3 nodes are gone.
    REQUIRE(cfg.get("cluster/node[0]/port") == "80");
    REQUIRE(cfg.get("cluster/node[1]/port") == "90");
    // Node[2] from the first layer must be absent.
    REQUIRE(cfg.get("cluster/node[2]/port") == std::nullopt);
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
// D-08: schema_enforcer canonical_text normalization
// ---------------------------------------------------------------------------

namespace {

key_path kp(const char *text) { return nucleus::key_path::parse(text).value(); }

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
          "[repeated_container][enforcer][D08]")
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
          "[repeated_container][enforcer][D08]")
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
// D-21: get_as() loud error; get() nullopt; get_all_as() typed gather
// ---------------------------------------------------------------------------

TEST_CASE("get_as on unindexed repeated container path returns errc::index_required",
          "[repeated_container][D21][get_as]")
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
    REQUIRE(result.error().message.find("2") != std::string::npos);

    // Indexed path must succeed (port is typed only if converter registered,
    // otherwise absent_key for untyped; but cluster/node[0]/port IS present).
    REQUIRE(cfg.get("cluster/node[0]/port") == "80");
}

TEST_CASE("get on unindexed repeated container path returns nullopt",
          "[repeated_container][D21][get]")
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
          "[repeated_container][D21][get_all_as]")
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
