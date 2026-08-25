#include "collection_shapes.h"

#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <string_view>

namespace {

nucleus::config_space identity_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::primary_key_element(
            "name", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::merging(
            nucleus::repeated_element("output", anchor::keyspace("cluster/server")),
            nucleus::merge_mode::unite)));
    REQUIRE(builder.register_element(
            nucleus::element("id", anchor::keyspace("cluster/server/output"))));
    REQUIRE(builder.register_element(
            nucleus::element("path", anchor::keyspace("cluster/server/output"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_ids", anchor::keyspace("cluster/server"))
                    .members({"output"})
                    .field("id")));
    return nucleus::builder_result_test::built(builder);
}

std::map<std::string, std::string> expected_snapshot()
{
    return {{"cluster/server/name", "alpha"},
            {"cluster/server/output[0]/id", "left"},
            {"cluster/server/output[0]/path", "/tmp/left"},
            {"cluster/server/output[1]/id", "right"},
            {"cluster/server/output[1]/path", "/tmp/right"}};
}

std::map<std::string, std::string> snapshot_of(const nucleus::config &config)
{
    std::map<std::string, std::string> snapshot;
    for(const std::string &key : config.keys())
    {
        const auto value = config.get(key);
        REQUIRE(value);
        snapshot.emplace(key, value.value());
    }
    return snapshot;
}

nucleus::source_handle xml_of(std::string document)
{
    return nucleus::source_handle(nucleus::xml_source::from(
            nucleus::xml_source_options::of_string(std::move(document))));
}

std::string identity_document()
{
    return "<cluster><server name=\"alpha\">"
           "<output><id>left</id><path>/tmp/left</path></output>"
           "<output><id>right</id><path>/tmp/right</path></output>"
           "</server></cluster>";
}

nucleus::load_result load_original(const nucleus::config_space &space)
{
    const std::string     document = identity_document();
    nucleus::load_options options;
    options.document_paths = {"identity.xml"};
    options.make_document  = [document](const std::string &)
    {
        return xml_of(document);
    };
    options.selection = "alpha";
    return nucleus::load_config(space, nucleus::source_stack{}, options);
}

nucleus::load_result reload(const nucleus::config_space &space,
                            const std::string           &document)
{
    nucleus::load_options options;
    options.selection = "alpha";
    return nucleus::load_config(
            space, nucleus::source_stack{xml_of(document)},
            options);
}

std::size_t count(std::string_view text, std::string_view needle)
{
    std::size_t matches = 0;
    for(std::size_t position = text.find(needle); position != std::string_view::npos;
        position             = text.find(needle, position + needle.size()))
        ++matches;
    return matches;
}

void require_identity_pairing(const nucleus::config &config)
{
    REQUIRE(config.get_all("cluster/server/output/id") ==
            std::vector<std::string>{"left", "right"});
    REQUIRE(config.get("cluster/server/output[0]/path") == "/tmp/left");
    REQUIRE(config.get("cluster/server/output[1]/path") == "/tmp/right");
}

void require_recomputed_metadata(const nucleus::config &before,
                                 const nucleus::config &after)
{
    REQUIRE(after.degradations().empty());
    for(const std::string &key : after.keys())
    {
        const nucleus::origin *old_origin = before.provenance_of(key);
        const nucleus::origin *new_origin = after.provenance_of(key);
        REQUIRE(old_origin != nullptr);
        REQUIRE(new_origin != nullptr);
        REQUIRE(old_origin->layer == "path:identity.xml");
        REQUIRE(new_origin->layer == "stack[0]");
        REQUIRE_FALSE(new_origin->inheritance_layer.has_value());
    }
}

}

TEST_CASE("attribute-projected identity survives a two-instance XML round trip",
          "[collection_shapes][identity][round_trip]")
{
    const nucleus::config_space space = identity_space();
    const nucleus::load_result  first = load_original(space);
    REQUIRE(first);
    REQUIRE(snapshot_of(first.value()) == expected_snapshot());
    auto rendered = nucleus::xml::render_document(first.value(), space);
    REQUIRE(rendered);
    REQUIRE(count(rendered.value(), "name=\"alpha\"") == 1);
    const nucleus::load_result second = reload(space, rendered.value());
    REQUIRE(second);
    REQUIRE(snapshot_of(second.value()) == expected_snapshot());
    require_identity_pairing(second.value());
    require_recomputed_metadata(first.value(), second.value());
}
