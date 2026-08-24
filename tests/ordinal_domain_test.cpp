#include "nucleus/error.h"
#include "nucleus/config.h"
#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/cli_surface.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <utility>

namespace {

const std::string at_bound = "4294967295";
const std::string above_bound = "4294967296";

nucleus::config_space cluster_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::element("port", nucleus::anchor::keyspace("cluster/node"))));
    return nucleus::builder_result_test::built(builder);
}

std::string indexed_path(const std::string &ordinal)
{
    return "cluster/node[" + ordinal + "]/port";
}

nucleus::runtime_source instance_base()
{
    nucleus::runtime_source source;
    source.set("cluster/node[0]/port", "80");
    return source;
}

nucleus::load_result load_argv(const nucleus::config_space &space,
                               const std::string           &ordinal)
{
    nucleus::argv_source argv(
            std::vector<std::string>{"--cluster-node-" + ordinal + "-port=90"});
    argv.recognize_with(nucleus::recognizer_of(space));
    return nucleus::load_config(
            space, nucleus::source_stack{instance_base(), std::move(argv)}, {});
}

nucleus::load_result load_runtime(const nucleus::config_space &space,
                                  const std::string           &ordinal)
{
    nucleus::runtime_source runtime;
    runtime.set(indexed_path(ordinal), "90");
    return nucleus::load_config(space, nucleus::source_stack{std::move(runtime)}, {});
}

void requires_report(const nucleus::load_result &loaded, const std::string &label,
                     const std::string &offending)
{
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::malformed_source);
    REQUIRE(loaded.error().message.find("source '" + label + "'") != std::string::npos);
    REQUIRE(loaded.error().message.find(offending) != std::string::npos);
}

}

TEST_CASE("an argv address above the ordinal bound is reported, not resolved",
          "[ordinal][domain][argv]")
{
    const nucleus::config_space space = cluster_space();

    requires_report(load_argv(space, above_bound), "stack[1]",
                    "cluster/node/" + above_bound + "/port");

    // The accepted neighbor is admitted as an ordinal and fails only on instance
    // range, so the report above pins a boundary and not a blanket refusal.
    const nucleus::load_result edge = load_argv(space, at_bound);
    REQUIRE_FALSE(edge);
    REQUIRE(edge.error().code == nucleus::errc::schema_violation);
    REQUIRE(edge.error().message.find("out of range") != std::string::npos);

    const nucleus::load_result inside = load_argv(space, "0");
    REQUIRE(inside);
    REQUIRE(inside.value().get("cluster/node[0]/port") == "90");
}

TEST_CASE("a runtime key above the ordinal bound is reported through load_config",
          "[ordinal][domain][runtime]")
{
    const nucleus::config_space space = cluster_space();

    requires_report(load_runtime(space, above_bound), "stack[0]",
                    indexed_path(above_bound));

    const nucleus::load_result edge = load_runtime(space, at_bound);
    REQUIRE(edge);
    REQUIRE(edge.value().get_all("cluster/node/port")
            == std::vector<std::string>{"90"});
}

TEST_CASE("a document key above the ordinal bound is reported at construction",
          "[ordinal][domain][document]")
{
    const auto above = nucleus::config::from_values(
            {{indexed_path(above_bound), "90"}});
    REQUIRE_FALSE(above);
    REQUIRE(above.error().code == nucleus::errc::malformed_source);
    REQUIRE(above.error().message.find(indexed_path(above_bound)) != std::string::npos);

    const auto edge = nucleus::config::from_values({{indexed_path(at_bound), "90"}});
    REQUIRE(edge);
    REQUIRE(edge.value().get(indexed_path(at_bound)) == "90");
}

TEST_CASE("every route shares the one bound key_path states", "[ordinal][domain]")
{
    REQUIRE(nucleus::key_path::max_ordinal == 4294967295ULL);
    REQUIRE(nucleus::key_path::is_indexed_segment("node[" + at_bound + "]"));
    REQUIRE_FALSE(nucleus::key_path::is_indexed_segment("node[" + above_bound + "]"));
}

TEST_CASE("both instance enumerations admit the bound ordinal and no other",
          "[ordinal][domain][enumeration]")
{
    const auto enumerated = nucleus::config::from_values(
            {{"cluster/node[0]/port", "80"}, {indexed_path(at_bound), "90"}});
    REQUIRE(enumerated);
    const nucleus::config_node nodes = enumerated.value().root()["cluster"]["node"];
    REQUIRE(nodes.count() == 2);
    REQUIRE(nodes.children().size() == 2);
    REQUIRE(nodes[static_cast<std::size_t>(4294967295)].exists());

    nucleus::runtime_source base;
    base.set("cluster/node[0]/port", "80");
    base.set(indexed_path(at_bound), "80");
    nucleus::argv_source argv(
            std::vector<std::string>{"--cluster-node-" + at_bound + "-port=90"});
    const nucleus::config_space space = cluster_space();
    argv.recognize_with(nucleus::recognizer_of(space));
    const nucleus::load_result appended = nucleus::load_config(
            space, nucleus::source_stack{std::move(base), std::move(argv)}, {});
    REQUIRE(appended);
    REQUIRE(appended.value().get(indexed_path(at_bound)) == "90");

    // An above-bound bracket ordinal never reaches either enumeration:
    // key_path::parse refuses the segment through is_indexed_segment.
    REQUIRE_FALSE(nucleus::config::from_values({{indexed_path(above_bound), "90"}}));
}
