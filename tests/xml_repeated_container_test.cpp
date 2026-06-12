// Ordinal emission for repeated-container instances: xml_source assigns zero-based
// ordinals in document order to sibling elements under a repeated schema container.
// Tests verify the raw batch entries emitted by xml_source::pull() (the fold is not
// yet aware of indexed paths; that is Plan 05).
// Plan 08: xml_emitter round-trip tests (D-22) appended below.

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using nucleus::anchor;
using nucleus::schema_registry;

namespace {

nucleus::xml_source xml_of(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

// Schema: cluster -> node (repeated) -> port (leaf)
schema_registry cluster_nodes_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace("cluster/node"))));
    return reg;
}

// Schema: cluster -> node (repeated) -> route (repeated) -> method (leaf)
schema_registry cluster_nodes_routes_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::repeated_element("route", anchor::keyspace("cluster/node"))));
    REQUIRE(reg.attach(nucleus::element("method", anchor::keyspace("cluster/node/route"))));
    return reg;
}

// Schema: cluster -> server (plain, non-repeated) -> port (leaf)
schema_registry cluster_plain_server_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace("cluster/server"))));
    return reg;
}

}

TEST_CASE("xml ordinal emission -- N node instances", "[xml][repeated_container][ordinal]")
{
    schema_registry reg = cluster_nodes_registry();
    nucleus::schema_projection proj = reg.projection();

    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto &entries = result.value().entries;

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for(const auto &e : entries)
        paths.push_back(e.path);

    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/node[0]/port") != paths.end());
    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/node[1]/port") != paths.end());

    // Ordinals are zero-based and in document order: [0] before [1].
    auto it0 = std::find(paths.begin(), paths.end(), "cluster/node[0]/port");
    auto it1 = std::find(paths.begin(), paths.end(), "cluster/node[1]/port");
    REQUIRE(it0 < it1);

    // Values match document order.
    for(const auto &e : entries)
    {
        if(e.path == "cluster/node[0]/port")
            REQUIRE(std::string(e.value.text()) == "80");
        if(e.path == "cluster/node[1]/port")
            REQUIRE(std::string(e.value.text()) == "90");
    }
}

TEST_CASE("xml nested ordinal emission", "[xml][repeated_container][nested]")
{
    schema_registry reg = cluster_nodes_routes_registry();
    nucleus::schema_projection proj = reg.projection();

    auto src = xml_of(
        "<cluster>"
        "<node>"
        "<route><method>fast</method></route>"
        "<route><method>slow</method></route>"
        "</node>"
        "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto &entries = result.value().entries;

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for(const auto &e : entries)
        paths.push_back(e.path);

    // Nested repetition composes: node[0]/route[0]/... and node[0]/route[1]/...
    REQUIRE(std::find(paths.begin(), paths.end(),
                      "cluster/node[0]/route[0]/method") != paths.end());
    REQUIRE(std::find(paths.begin(), paths.end(),
                      "cluster/node[0]/route[1]/method") != paths.end());

    for(const auto &e : entries)
    {
        if(e.path == "cluster/node[0]/route[0]/method")
            REQUIRE(std::string(e.value.text()) == "fast");
        if(e.path == "cluster/node[0]/route[1]/method")
            REQUIRE(std::string(e.value.text()) == "slow");
    }
}

// Helper: build a config_space with cluster -> node (repeated) -> port + metrics/latency.
namespace {

nucleus::config_space make_cluster_space_for_emitter()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::element("port", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(nucleus::element("metrics", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(nucleus::element("latency", anchor::keyspace("cluster/node/metrics"))));
    return builder.build();
}

nucleus::source_handle xml_source_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

nucleus::load_options doc_opts(const std::string &xml)
{
    nucleus::load_options opts;
    opts.document_paths = {"doc.xml"};
    opts.make_document = [xml](const std::string &) { return xml_source_of(xml); };
    return opts;
}

} // namespace

TEST_CASE("non-repeated container -- no ordinals assigned", "[xml][repeated_container][plain]")
{
    schema_registry reg = cluster_plain_server_registry();
    nucleus::schema_projection proj = reg.projection();

    auto src = xml_of(
        "<cluster>"
        "<server><port>80</port></server>"
        "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto &entries = result.value().entries;

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for(const auto &e : entries)
        paths.push_back(e.path);

    // Plain non-repeated container: no ordinal suffix.
    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/server/port") != paths.end());

    // No indexed paths produced for a plain container.
    for(const auto &p : paths)
        REQUIRE(p.find('[') == std::string::npos);
}

TEST_CASE("xml emitter -- repeated container bracket strip", "[xml][xml_emitter][D22]")
{
    const nucleus::config_space space = make_cluster_space_for_emitter();

    const std::string kXml =
        "<cluster>"
        "<node><port>1.5</port><metrics><latency>0.1</latency></metrics></node>"
        "<node><port>2.0</port><metrics><latency>0.2</latency></metrics></node>"
        "</cluster>";

    auto loaded = nucleus::load_config(space, nucleus::source_stack{}, doc_opts(kXml));
    REQUIRE(loaded);

    std::ostringstream out;
    nucleus::xml::emit_document(loaded.value(), out);
    const std::string emitted = out.str();

    // No bracket-suffixed element names in the output.
    REQUIRE(emitted.find("node[0]") == std::string::npos);
    REQUIRE(emitted.find("node[1]") == std::string::npos);

    // Both <node> elements appear as siblings (at least two occurrences of <node>).
    std::size_t count = 0;
    std::size_t pos = 0;
    while((pos = emitted.find("<node>", pos)) != std::string::npos)
    {
        ++count;
        ++pos;
    }
    REQUIRE(count >= 2);

    // Values from both instances are present.
    REQUIRE(emitted.find("1.5") != std::string::npos);
    REQUIRE(emitted.find("2.0") != std::string::npos);
    REQUIRE(emitted.find("0.1") != std::string::npos);
    REQUIRE(emitted.find("0.2") != std::string::npos);
}

TEST_CASE("xml emitter -- repeated container round-trip", "[xml][xml_emitter][round_trip][D22]")
{
    const nucleus::config_space space = make_cluster_space_for_emitter();

    const std::string kXml =
        "<cluster>"
        "<node><port>1.5</port><metrics><latency>0.1</latency></metrics></node>"
        "<node><port>2.0</port><metrics><latency>0.2</latency></metrics></node>"
        "</cluster>";

    auto original = nucleus::load_config(space, nucleus::source_stack{}, doc_opts(kXml));
    REQUIRE(original);

    // Emit to a string, then re-load.
    std::ostringstream out;
    nucleus::xml::emit_document(original.value(), out);

    auto reloaded = nucleus::load_config(space, nucleus::source_stack{}, doc_opts(out.str()));
    REQUIRE(reloaded);

    // Round-trip: indexed paths must survive.
    REQUIRE(reloaded.value().get("cluster/node[0]/port") == "1.5");
    REQUIRE(reloaded.value().get("cluster/node[1]/port") == "2.0");
    REQUIRE(reloaded.value().get("cluster/node[0]/metrics/latency") == "0.1");
    REQUIRE(reloaded.value().get("cluster/node[1]/metrics/latency") == "0.2");
}
