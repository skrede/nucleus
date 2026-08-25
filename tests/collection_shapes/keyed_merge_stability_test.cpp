#include "collection_shapes.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>

namespace {

using entries = std::vector<std::pair<std::string, std::string>>;

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
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space nested_output_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::element("server", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::primary_key_element(
            "name", nucleus::anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::merging(
            nucleus::repeated_element("output", nucleus::anchor::keyspace("cluster/server")),
            nucleus::merge_mode::unite)));
    REQUIRE(builder.register_element(
            nucleus::element("id", nucleus::anchor::keyspace("cluster/server/output"))));
    REQUIRE(builder.register_element(
            nucleus::element("path", nucleus::anchor::keyspace("cluster/server/output"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_ids", nucleus::anchor::keyspace("cluster/server"))
                    .members({"output"})
                    .field("id")));
    return nucleus::builder_result_test::built(builder);
}

entries base_outputs()
{
    return {{"endpoints/output[0]/name", "a"},
            {"endpoints/output[0]/path", "/a"},
            {"endpoints/output[1]/name", "b"},
            {"endpoints/output[1]/path", "/b"},
            {"endpoints/output[2]/name", "c"},
            {"endpoints/output[2]/path", "/c"}};
}

entries output(const std::string &name, const std::string &path)
{
    return {{"endpoints/output[0]/name", name},
            {"endpoints/output[0]/path", path}};
}

nucleus::load_result load_outputs(const nucleus::config_space &space,
                                  entries base, entries higher)
{
    return nucleus::load_config(
            space,
            nucleus::source_stack{nucleus::shapes::runtime_layer(std::move(base)),
                                  nucleus::shapes::runtime_layer(std::move(higher))},
            {});
}

std::vector<std::string> names_of(const nucleus::config &config)
{
    return config.get_all("endpoints/output/name");
}

}

TEST_CASE("replacing an identifier preserves its first ordinal and its surviving origin",
          "[collection_shapes][keyed][merge][regression][pin]")
{
    const nucleus::config_space space = output_space(nucleus::merge_mode::replace_by_key);

    SECTION("the middle identifier keeps the middle ordinal")
    {
        const nucleus::load_result loaded = load_outputs(space, base_outputs(), output("b", "/new-b"));
        REQUIRE(loaded);
        INFO(nucleus::shapes::serialize(loaded.value()));
        REQUIRE(names_of(loaded.value()) == std::vector<std::string>{"a", "b", "c"});
        REQUIRE(loaded.value().get_all("endpoints/output/name").size() == 3);
        REQUIRE(nucleus::shapes::serialize(loaded.value()) == "endpoints/output[0]/name = a [0|stack[0]|anonymous|-]\n"
                                                              "endpoints/output[0]/path = /a [0|stack[0]|anonymous|-]\n"
                                                              "endpoints/output[1]/name = b [1|stack[1]|anonymous|-]\n"
                                                              "endpoints/output[1]/path = /new-b [1|stack[1]|anonymous|-]\n"
                                                              "endpoints/output[2]/name = c [0|stack[0]|anonymous|-]\n"
                                                              "endpoints/output[2]/path = /c [0|stack[0]|anonymous|-]\n");
    }

    SECTION("the first identifier keeps the first ordinal")
    {
        const nucleus::load_result loaded = load_outputs(space, base_outputs(), output("a", "/new-a"));
        REQUIRE(loaded);
        INFO(nucleus::shapes::serialize(loaded.value()));
        REQUIRE(names_of(loaded.value()) == std::vector<std::string>{"a", "b", "c"});
        REQUIRE(loaded.value().get("endpoints/output[0]/path") == "/new-a");
    }

    SECTION("a new identifier appends after existing identifiers")
    {
        const nucleus::load_result loaded = load_outputs(space, base_outputs(), output("d", "/d"));
        REQUIRE(loaded);
        INFO(nucleus::shapes::serialize(loaded.value()));
        REQUIRE(names_of(loaded.value()) == std::vector<std::string>{"a", "b", "c", "d"});
        REQUIRE(loaded.value().get("endpoints/output[3]/path") == "/d");
    }
}

TEST_CASE("the additive keyed merge retains its stable layer and source order",
          "[collection_shapes][keyed][merge][pin]")
{
    const nucleus::config_space space  = output_space(nucleus::merge_mode::unite);
    const nucleus::load_result  loaded = load_outputs(space, base_outputs(), output("d", "/d"));
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));
    REQUIRE(names_of(loaded.value()) == std::vector<std::string>{"a", "b", "c", "d"});
    REQUIRE(loaded.value().get("endpoints/output[3]/path") == "/d");

    const nucleus::load_result duplicate = load_outputs(space, output("a", "/a"),
                                                        output("a", "/higher"));
    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error().code == nucleus::errc::layering_violation);
    REQUIRE(duplicate.error().message == "keyed collection 'endpoints/output': identifier 'name'='a' is introduced "
                                         "at two layers ('stack[0]' and 'stack[1]'); unite is strict-additive "
                                         "(no override across layers)");
}

TEST_CASE("one keyed instance and an empty higher layer preserve the lower collection",
          "[collection_shapes][keyed][merge]")
{
    const nucleus::config_space space = output_space(nucleus::merge_mode::replace_by_key);

    SECTION("one instance resolves at ordinal zero")
    {
        const nucleus::load_result loaded = load_outputs(space, output("a", "/a"), {});
        REQUIRE(loaded);
        REQUIRE(names_of(loaded.value()) == std::vector<std::string>{"a"});
        REQUIRE(loaded.value().get("endpoints/output[0]/path") == "/a");
    }

    SECTION("an empty higher layer leaves all lower instances untouched")
    {
        const nucleus::load_result loaded = load_outputs(space, base_outputs(), {});
        REQUIRE(loaded);
        REQUIRE(names_of(loaded.value()) == std::vector<std::string>{"a", "b", "c"});
        REQUIRE(loaded.value().get_all("endpoints/output/path") == std::vector<std::string>{"/a", "/b", "/c"});
    }
}

TEST_CASE("a nested keyed merge keeps instances contributed across selected strain layers",
          "[collection_shapes][keyed][merge][nested][pin]")
{
    const nucleus::config_space space = nested_output_space();
    nucleus::load_options       options;
    options.selection                 = "web";
    const nucleus::load_result loaded = nucleus::load_config(
            space,
            nucleus::source_stack{
                    nucleus::shapes::runtime_layer({{"cluster/server/web/output[0]/id", "a"},
                                                    {"cluster/server/web/output[0]/path", "/a"}}),
                    nucleus::shapes::runtime_layer({{"cluster/server/web/output[0]/id", "b"},
                                                    {"cluster/server/web/output[0]/path", "/b"}})},
            options);
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));
    REQUIRE(loaded.value().get_all("cluster/server/output/id") == std::vector<std::string>{"a", "b"});
    REQUIRE(loaded.value().get_all("cluster/server/output/path") == std::vector<std::string>{"/a", "/b"});
}
