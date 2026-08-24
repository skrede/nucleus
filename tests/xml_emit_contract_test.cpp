#include "builder_result_test_support.h"
#include "nucleus/config_emitter.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <utility>

namespace {

nucleus::config_space make_template_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::element(
            "host", nucleus::anchor::keyspace("server"))));
    REQUIRE(builder.register_element(nucleus::enum_element(
            "mode", nucleus::anchor::keyspace("server"),
            std::vector<std::string>{"primary", "secondary"})));
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space make_document_space(bool required_key = false)
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::element(
            "node", nucleus::anchor::keyspace("cluster"))));
    auto key = nucleus::primary_key_element(
            "id", nucleus::anchor::keyspace("cluster/node"));
    key.required = required_key;
    REQUIRE(builder.register_element(std::move(key)));
    REQUIRE(builder.register_element(nucleus::element(
            "port", nucleus::anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(nucleus::element(
            "note", nucleus::anchor::keyspace("cluster/node"))));
    return nucleus::builder_result_test::built(builder);
}

nucleus::config config_of(std::map<std::string, std::string> values)
{
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

void check_safe_rejection(const nucleus::config_space       &space,
                          std::map<std::string, std::string> values,
                          const std::string                 &key)
{
    const nucleus::config config = config_of(std::move(values));
    std::ostringstream    output;
    auto                  result = nucleus::xml::emit_document(config, space, output);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == nucleus::errc::malformed_source);
    CHECK(result.error().message.find(key) != std::string::npos);
    CHECK(output.str().empty());
}

}

TEST_CASE("owned XML templates match checked delivery", "[xml][emit][contract]")
{
    const nucleus::config_space space = make_template_space();
    auto                        owned = nucleus::xml::render_template(space);
    REQUIRE(owned);
    std::ostringstream delivered;
    REQUIRE(nucleus::xml::emit_template(space, delivered));
    CHECK(delivered.str() == owned.value());
}

TEST_CASE("owned XML templates preserve wrappers and annotations",
          "[xml][emit][contract]")
{
    const nucleus::config_space space = make_template_space();
    auto                        owned = nucleus::xml::render_template(space, "profile");
    REQUIRE(owned);
    CHECK(owned.value().find("<profile>") != std::string::npos);
    CHECK(owned.value().find("allowed=\"primary|secondary\"") !=
          std::string::npos);
    CHECK(owned.value().find("<host") != std::string::npos);
}

TEST_CASE("safe XML rendering accepts compatible optional shapes",
          "[xml][emit][contract]")
{
    const nucleus::config_space space  = make_document_space();
    const nucleus::config       config = config_of({{"cluster/node/id", "left"}, {"cluster/node/port", "80"}});
    auto                        owned  = nucleus::xml::render_document(config, space);
    REQUIRE(owned);
    CHECK(owned.value().find("<node id=\"left\">") != std::string::npos);
    CHECK(owned.value().find("<id>") == std::string::npos);
    CHECK(owned.value().find("<port>80</port>") != std::string::npos);
    REQUIRE(nucleus::xml::render_document(
            config_of({{"cluster/node/port", "81"}}), space));
}

TEST_CASE("safe XML rejects schema and storage role conflicts before delivery",
          "[xml][emit][contract][matrix]")
{
    const nucleus::config_space space = make_document_space();
    SECTION("unknown path")
    {
        check_safe_rejection(space, {{"cluster/node/extra", "x"}},
                             "cluster/node/extra");
    }
    SECTION("ordinal on scalar")
    {
        check_safe_rejection(space, {{"cluster/node/port[0]", "80"}},
                             "cluster/node/port[0]");
    }
    SECTION("missing repeated scalar ordinal")
    {
        nucleus::config_space_builder builder;
        REQUIRE(builder.register_element(
                nucleus::repeated_element("tag", nucleus::anchor::root())));
        check_safe_rejection(nucleus::builder_result_test::built(builder), {{"tag", "value"}}, "tag");
    }
    SECTION("missing repeated container ordinal")
    {
        nucleus::config_space_builder builder;
        REQUIRE(builder.register_element(
                nucleus::repeated_element("node", nucleus::anchor::root())));
        REQUIRE(builder.register_element(nucleus::element(
                "port", nucleus::anchor::keyspace("node"))));
        check_safe_rejection(nucleus::builder_result_test::built(builder), {{"node/port", "80"}},
                             "node/port");
    }
    SECTION("value on structural container")
    {
        check_safe_rejection(space, {{"cluster/node", "value"}},
                             "cluster/node");
    }
}

TEST_CASE("safe XML enforces primary-key requiredness and cardinality",
          "[xml][emit][contract][matrix]")
{
    SECTION("required key is absent")
    {
        check_safe_rejection(make_document_space(true),
                             {{"cluster/node/port", "80"}},
                             "cluster/node/id");
    }
    SECTION("one parent carries indexed key values")
    {
        check_safe_rejection(
                make_document_space(),
                {{"cluster/node/id[0]", "left"},
                 {"cluster/node/id[1]", "right"}},
                "cluster/node/id[0]");
    }
}

TEST_CASE("a malformed name never reaches the emitter and a value is checked before mutation",
          "[xml][emit][contract][matrix]")
{
    nucleus::config_space_builder builder;
    CHECK_FALSE(builder.register_element(
            nucleus::element("bad name", nucleus::anchor::root())));
    const nucleus::config_space space = make_document_space();
    check_safe_rejection(space, {{"cluster/node/port", "a\rb"}},
                         "cluster/node/port");
}

TEST_CASE("explicit schema-blind XML keeps universal validation",
          "[xml][emit][contract]")
{
    const nucleus::config config = config_of({{"outside/value", "accepted"}});
    auto                  owned  = nucleus::xml::render_document_schema_blind(config);
    REQUIRE(owned);
    std::ostringstream output;
    REQUIRE(nucleus::xml::emit_document_schema_blind(config, output));
    CHECK(output.str() == owned.value());
    const nucleus::config invalid  = config_of({{"outside/value", "a\x01"}});
    auto                  rejected = nucleus::xml::render_document_schema_blind(invalid);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == nucleus::errc::malformed_source);
    CHECK(rejected.error().message.find("outside/value") != std::string::npos);
}

static_assert(nucleus::config_emitter<nucleus::xml::emitter>);
