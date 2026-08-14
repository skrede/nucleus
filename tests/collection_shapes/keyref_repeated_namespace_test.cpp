#include "nucleus/query/query.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace {

nucleus::config_space repeated_namespace_space(const bool require_global = false)
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("output", nucleus::anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("cluster/node/output"))));
    REQUIRE(builder.register_identity_group(nucleus::identity_group(
                                                    "output_names", nucleus::anchor::keyspace("cluster/node"))
                                                    .members({"output"})
                                                    .field("name")));
    REQUIRE(builder.register_element(nucleus::keyref_element(
            "local_target", nucleus::anchor::keyspace("cluster/node/output"), "output_names")));
    auto global = nucleus::keyref_element(
            "global_target", nucleus::anchor::keyspace("cluster"), "output_names");
    global.required = require_global;
    REQUIRE(builder.register_element(std::move(global)));
    return std::move(builder).build();
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

std::string node(const std::vector<std::string> &identities,
                 const std::string              &local_reference = {})
{
    std::string text = "<node>";
    for(std::size_t i = 0; i < identities.size(); ++i)
    {
        text += "<output><name>" + identities[i] + "</name>";
        if(i == 0 && !local_reference.empty())
            text += "<local_target>" + local_reference + "</local_target>";
        text += "</output>";
    }
    return text + "</node>";
}

std::string bound_document()
{
    return "<cluster>" + node({"shared"}, "shared") + node({"shared"}, "shared") + "</cluster>";
}

std::string partially_bound_document(const std::size_t match_count)
{
    std::vector<std::string> local{"other"};
    local.insert(local.end(), match_count, "shared");
    return "<cluster>" + node(local, "shared") + node({"shared"}) + "</cluster>";
}

std::string globally_unbound_document(const std::size_t match_count,
                                      const bool        include_reference = true)
{
    std::string text = "<cluster>" + node({"other"});
    for(std::size_t i = 0; i < match_count; ++i)
        text += node({"shared"});
    if(include_reference)
        text += "<global_target>shared</global_target>";
    return text + "</cluster>";
}

nucleus::load_result load(const nucleus::config_space &space, const std::string &document)
{
    return nucleus::load_config(space, nucleus::source_stack{xml_of(document)}, {});
}

void require_target(const nucleus::config &config, const nucleus::schema_query_context &ctx,
                    const std::string &reference_path, const std::string &target_path)
{
    const auto target = nucleus::follow_keyref(
            nucleus::config_node{&config, reference_path}, ctx);
    REQUIRE(target.has_value());
    REQUIRE(target->path() == target_path);
}

void require_load_error(const nucleus::config_space &space, const std::string &document,
                        const std::string &path, const std::string &scope,
                        const std::size_t count)
{
    const nucleus::load_result loaded = load(space, document);
    REQUIRE_FALSE(loaded.has_value());
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message.find(path) != std::string::npos);
    REQUIRE(loaded.error().message.find("shared") != std::string::npos);
    REQUIRE(loaded.error().message.find("output_names") != std::string::npos);
    REQUIRE(loaded.error().message.find(scope) != std::string::npos);
    REQUIRE(loaded.error().message.find(std::to_string(count) + " target") != std::string::npos);
}

}

TEST_CASE("Keyrefs bind a shared concrete repeated parent", "[collection_shapes][keyref]")
{
    const nucleus::config_space space  = repeated_namespace_space();
    const auto                  ctx    = space.query_context();
    const nucleus::load_result  loaded = load(space, bound_document());
    INFO((loaded.has_value() ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    for(const std::size_t index : {std::size_t{0}, std::size_t{1}})
    {
        const std::string prefix = "cluster/node[" + std::to_string(index) + "]/output[0]";
        require_target(*loaded, ctx, prefix + "/local_target", prefix);
    }
}

TEST_CASE("Bound keyrefs search every unbound target instance", "[collection_shapes][keyref]")
{
    const nucleus::config_space space  = repeated_namespace_space();
    const auto                  ctx    = space.query_context();
    const nucleus::load_result  loaded = load(space, partially_bound_document(1));
    INFO((loaded.has_value() ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    require_target(*loaded, ctx, "cluster/node[0]/output[0]/local_target",
                   "cluster/node[0]/output[1]");
    for(const std::size_t count : {std::size_t{0}, std::size_t{2}, std::size_t{10}})
        require_load_error(space, partially_bound_document(count),
                           "cluster/node[0]/output[0]/local_target", "cluster/node[0]", count);
}

TEST_CASE("Unbound keyrefs require one global target", "[collection_shapes][keyref]")
{
    const nucleus::config_space space  = repeated_namespace_space();
    const auto                  ctx    = space.query_context();
    const nucleus::load_result  loaded = load(space, globally_unbound_document(1));
    REQUIRE(loaded.has_value());
    require_target(*loaded, ctx, "cluster/global_target", "cluster/node[1]/output[0]");
    for(const std::size_t count : {std::size_t{0}, std::size_t{2}, std::size_t{10}})
        require_load_error(space, globally_unbound_document(count),
                           "cluster/global_target", "<unbound>", count);
}

TEST_CASE("Keyref absence remains controlled by required", "[collection_shapes][keyref]")
{
    const std::string document = globally_unbound_document(1, false);
    REQUIRE(load(repeated_namespace_space(), document).has_value());
    const nucleus::load_result required = load(repeated_namespace_space(true), document);
    REQUIRE_FALSE(required.has_value());
    REQUIRE(required.error().code == nucleus::errc::schema_violation);
    REQUIRE(required.error().message.find("global_target") != std::string::npos);
}
