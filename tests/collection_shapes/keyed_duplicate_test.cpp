#include "collection_shapes.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace {

nucleus::config_space output_space(nucleus::merge_mode mode)
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("endpoints", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::merging(
            nucleus::repeated_element("output", nucleus::anchor::keyspace("endpoints")), mode)));
    REQUIRE(builder.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("endpoints/output"))));
    REQUIRE(builder.register_element(
            nucleus::element("path", nucleus::anchor::keyspace("endpoints/output"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_names", nucleus::anchor::keyspace("endpoints"))
                    .members({"output"})
                    .field("name")));
    return builder.build();
}

nucleus::config_space strain_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::element("server", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(
            nucleus::element("port", nucleus::anchor::keyspace("cluster/server"))));
    return builder.build();
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

nucleus::load_result load_duplicate_outputs(nucleus::merge_mode mode)
{
    return nucleus::load_config(
            output_space(mode),
            nucleus::source_stack{xml_of(
                    "<endpoints>"
                    "<output><name>a</name><path>/one</path></output>"
                    "<output><name>a</name><path>/two</path></output>"
                    "<output><name>b</name><path>/three</path></output>"
                    "</endpoints>")},
            {});
}

bool mentions(const nucleus::error &error, const char *text)
{
    return error.message.find(text) != std::string::npos;
}

std::string duplicate_document(std::size_t first, std::size_t second)
{
    std::string document = "<endpoints>";
    for(std::size_t position = 0; position < 4; ++position)
    {
        const std::string name = position == first || position == second
                ? "dup"
                : "unique-" + std::to_string(position);
        document += "<output><name>" + name + "</name><path>/";
        document += std::to_string(position) + "</path></output>";
    }
    return document + "</endpoints>";
}

}

TEST_CASE("a same-layer duplicate in a replacing keyed merge is a loud layering error",
          "[collection_shapes][keyed][duplicate]")
{
    const nucleus::load_result loaded   = load_duplicate_outputs(nucleus::merge_mode::replace_by_key);
    const std::string          observed = loaded.has_value()
            ? nucleus::shapes::serialize(loaded.value())
            : loaded.error().message;
    INFO(observed);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
    REQUIRE(mentions(loaded.error(), "endpoints/output"));
    REQUIRE(mentions(loaded.error(), "identifier 'name'='a'"));
    REQUIRE(mentions(loaded.error(), "within layer 'stack[0]'"));
}

TEST_CASE("the additive keyed merge retains its exact same-layer duplicate diagnostic",
          "[collection_shapes][keyed][duplicate]")
{
    const nucleus::load_result loaded = load_duplicate_outputs(nucleus::merge_mode::unite);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
    REQUIRE(loaded.error().message == "keyed collection 'endpoints/output': identifier 'name'='a' is duplicated "
                                      "within layer 'stack[0]'; unite is strict-additive (no duplicate "
                                      "identifiers within one layer)");
}

TEST_CASE("a replacing keyed merge still accepts one identifier across different layers",
          "[collection_shapes][keyed][duplicate][pin]")
{
    const nucleus::config_space space  = output_space(nucleus::merge_mode::replace_by_key);
    const nucleus::load_result  loaded = nucleus::load_config(
            space,
            nucleus::source_stack{
                    nucleus::shapes::runtime_layer({{"endpoints/output[0]/name", "a"},
                                                    {"endpoints/output[0]/path", "/base"}}),
                    nucleus::shapes::runtime_layer({{"endpoints/output[0]/name", "a"},
                                                    {"endpoints/output[0]/path", "/higher"}})},
            {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get_all("endpoints/output/name") == std::vector<std::string>{"a"});
    REQUIRE(loaded.value().get("endpoints/output[0]/path") == "/higher");
}

TEST_CASE("duplicate keyed strains in one document fail at the source boundary",
          "[collection_shapes][keyed][duplicate][pin]")
{
    const nucleus::load_result loaded = nucleus::load_config(
            strain_space(),
            nucleus::source_stack{xml_of(
                    "<cluster>"
                    "<server name=\"alpha\"><port>80</port></server>"
                    "<server name=\"alpha\"><port>443</port></server>"
                    "</cluster>")},
            {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::malformed_source);
    REQUIRE(mentions(loaded.error(), "duplicate primary-key value 'alpha'"));
    REQUIRE(mentions(loaded.error(), "container 'cluster/server'"));
}

TEST_CASE("same-layer keyed duplicates are loud at every pair of source positions",
          "[collection_shapes][keyed][duplicate][sweep]")
{
    std::size_t shape_count = 0;
    for(const nucleus::merge_mode mode : {nucleus::merge_mode::unite,
                                          nucleus::merge_mode::replace_by_key})
    {
        for(std::size_t first = 0; first < 4; ++first)
        {
            for(std::size_t second = first + 1; second < 4; ++second)
            {
                const nucleus::load_result loaded = nucleus::load_config(
                        output_space(mode),
                        nucleus::source_stack{xml_of(duplicate_document(first, second))}, {});
                INFO("positions " << first << ", " << second);
                REQUIRE_FALSE(loaded);
                REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
                REQUIRE(mentions(loaded.error(), "identifier 'name'='dup'"));
                REQUIRE(mentions(loaded.error(), "within layer 'stack[0]'"));
                ++shape_count;
            }
        }
    }
    REQUIRE(shape_count == 12);
}
