#include "collection_shapes.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

namespace {

constexpr const char *did_you_mean = "did you mean";

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

// cluster / node[] / { port (required), label, id (unique) }.
nucleus::config_space cluster_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::required_element("port", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::element("label", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::unique_element("id", anchor::keyspace("cluster/node"))));
    return builder.build();
}

nucleus::load_result load(const nucleus::config_space &space, const std::string &doc)
{
    return nucleus::load_config(space, nucleus::source_stack{xml_of(doc)}, {});
}

}

TEST_CASE("a document whose second instance omits a required child fails naming that "
          "instance and offers no suggestion",
          "[collection_shapes][report]")
{
    const nucleus::config_space space = cluster_space();

    const nucleus::load_result loaded = load(space,
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><label>second</label></node>"
        "</cluster>");
    REQUIRE_FALSE(loaded);
    INFO(loaded.error().message);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message.find("required field 'cluster/node[1]/port' is missing")
            != std::string::npos);
    REQUIRE(loaded.error().message.find(did_you_mean) == std::string::npos);
}

TEST_CASE("a genuinely misspelled path still carries a did-you-mean",
          "[collection_shapes][report]")
{
    const nucleus::config_space space = cluster_space();

    const nucleus::load_result loaded = load(space,
        "<cluster><node><port>80</port><lable>first</lable></node></cluster>");
    REQUIRE_FALSE(loaded);
    INFO(loaded.error().message);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message.find("cluster/node[0]/lable") != std::string::npos);
    REQUIRE(loaded.error().message.find("did you mean 'cluster/node/label'?")
            != std::string::npos);
}

TEST_CASE("a uniqueness violation carries no suggestion naming its own canonical form",
          "[collection_shapes][report]")
{
    const nucleus::config_space space = cluster_space();

    const nucleus::load_result loaded = load(space,
        "<cluster>"
        "<node><port>80</port><id>alpha</id></node>"
        "<node><port>90</port><id>alpha</id></node>"
        "</cluster>");
    REQUIRE_FALSE(loaded);
    INFO(loaded.error().message);
    REQUIRE(loaded.error().message.find("unique field 'cluster/node/id' has duplicate")
            != std::string::npos);
    REQUIRE(loaded.error().message.find(did_you_mean) == std::string::npos);
}
