#include "collection_shapes.h"

#include "nucleus/config.h"
#include "../builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

nucleus::config_space output_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("endpoints", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::merging(
            nucleus::repeated_element("output", nucleus::anchor::keyspace("endpoints")),
            nucleus::merge_mode::replace_by_key)));
    REQUIRE(builder.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("endpoints/output"))));
    REQUIRE(builder.register_element(
            nucleus::element("path", nucleus::anchor::keyspace("endpoints/output"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_names", nucleus::anchor::keyspace("endpoints"))
                    .members({"output"})
                    .field("name")));
    return nucleus::builder_result_test::built(builder);
}

nucleus::source_handle base_document(const std::string &)
{
    const std::string text = "<endpoints>"
                             "<output><name>a</name><path>/base-a</path></output>"
                             "<output><name>b</name><path>/base-b</path></output>"
                             "</endpoints>";
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

nucleus::load_result load_replacement(const nucleus::config_space &space)
{
    nucleus::load_options options;
    options.document_paths = {"base.xml"};
    options.make_document  = base_document;
    return nucleus::load_config(
            space,
            nucleus::source_stack{nucleus::shapes::runtime_layer(
                    {{"endpoints/output[0]/name", "b"},
                     {"endpoints/output[0]/path", "/higher-b"}})},
            options);
}

}

TEST_CASE("an XML keyed instance replaced by a runtime layer retains its first ordinal",
          "[collection_shapes][keyed][merge][mixed_source]")
{
    const nucleus::load_result loaded = load_replacement(output_space());
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));
    REQUIRE(loaded->get_all("endpoints/output/name") ==
            std::vector<std::string>{"a", "b"});
    REQUIRE(loaded->get("endpoints/output[1]/path") == "/higher-b");
    REQUIRE_FALSE(loaded->contains("endpoints/output[2]/name"));
    REQUIRE(nucleus::shapes::serialize(loaded.value()) ==
            "endpoints/output[0]/name = a [0|path:base.xml|anonymous|0]\n"
            "endpoints/output[0]/path = /base-a [0|path:base.xml|anonymous|0]\n"
            "endpoints/output[1]/name = b [1|stack[0]|anonymous|-]\n"
            "endpoints/output[1]/path = /higher-b [1|stack[0]|anonymous|-]\n");
}
