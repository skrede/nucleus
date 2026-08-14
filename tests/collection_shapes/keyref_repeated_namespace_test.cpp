#include "nucleus/query/query.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <cstddef>
#include <utility>

namespace {

nucleus::config_space repeated_namespace_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("output", nucleus::anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("cluster/node/output"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_names", nucleus::anchor::keyspace("cluster/node"))
                    .members({"output"})
                    .field("name")));
    REQUIRE(builder.register_element(
            nucleus::element("route", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::keyref_element(
            "target", nucleus::anchor::keyspace("cluster/route"), "output_names")));
    return std::move(builder).build();
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

std::string repeated_document(const std::size_t parent_count, const bool reuse_identity)
{
    std::string document = "<cluster>";
    std::string target_identity;
    for(std::size_t parent = 0; parent < parent_count; ++parent)
    {
        const std::string identity = reuse_identity
                ? "shared"
                : "node-" + std::to_string(parent);
        target_identity            = identity;
        document += "<node><output><name>" + identity + "</name></output></node>";
    }
    return document + "<route><target>" + target_identity + "</target></route></cluster>";
}

void check_absent_dereference(const nucleus::config_space         &space,
                              const nucleus::schema_query_context &ctx,
                              const std::size_t parent_count, const bool reuse_identity)
{
    const nucleus::load_result loaded = nucleus::load_config(
            space, nucleus::source_stack{xml_of(repeated_document(parent_count, reuse_identity))}, {});
    const std::string diagnostic = loaded.has_value() ? std::string{} : loaded.error().message;
    CAPTURE(reuse_identity, parent_count);
    INFO(diagnostic);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->get("cluster/node[0]/output[0]/name") ==
            (reuse_identity ? "shared" : "node-0"));
    REQUIRE(loaded->get("cluster/node[1]/output[0]/name") ==
            (reuse_identity ? "shared" : "node-1"));
    const nucleus::config_node keyref = loaded->root()["cluster"]["route"]["target"];
    REQUIRE(keyref.value() == (reuse_identity ? "shared" : "node-" + std::to_string(parent_count - 1)));
    const nucleus::expected<nucleus::config_node, nucleus::error> target =
            nucleus::follow_keyref(keyref, ctx);
    REQUIRE_FALSE(target.has_value());
    REQUIRE(target.error().code == nucleus::errc::absent_key);
}

}

TEST_CASE("follow_keyref reports absence when its namespace is nested beneath repetition",
          "[collection_shapes][keyref]")
{
    const nucleus::config_space         space = repeated_namespace_space();
    const nucleus::schema_query_context ctx   = space.query_context();
    constexpr std::array                parent_counts{std::size_t{2}, std::size_t{3}, std::size_t{5}};
    for(const bool reuse_identity : {false, true})
        for(const std::size_t parent_count : parent_counts)
            check_absent_dereference(space, ctx, parent_count, reuse_identity);
}
