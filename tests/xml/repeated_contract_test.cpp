#include "xml/repeated_test_support.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <sstream>
#include <utility>

namespace test = nucleus::xml_repeated_test;

TEST_CASE("xml emitter -- a sparse ordinal fails loudly and writes nothing",
          "[xml][xml_emitter]")
{
    const nucleus::config config = test::checked_config(
            {{"cluster/node[2]/port", "9"}});
    std::ostringstream output;
    const auto         result =
            nucleus::xml::emit_document_schema_blind(config, output);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(result.error().message.find("cluster/node") != std::string::npos);
    REQUIRE(output.str().empty());
}

TEST_CASE("xml emitter -- a sparse indexed LEAF ordinal fails loudly",
          "[xml][xml_emitter]")
{
    const nucleus::config config = test::checked_config(
            {{"cluster/tags[2]", "9"}});
    std::ostringstream output;
    const auto         result =
            nucleus::xml::emit_document_schema_blind(config, output);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(result.error().message.find("cluster/tags") != std::string::npos);
    REQUIRE(output.str().empty());
}

TEST_CASE("a declared repeated container carrying only text is rejected on read",
          "[xml][repeated_container][malformed]")
{
    const nucleus::config_space space  = test::cluster_space();
    const auto                  loaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(
                    "<cluster><node>oops</node>"
                    "<node><port>90</port></node></cluster>"));
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::malformed_source);
    CHECK(loaded.error().message.find("character data") != std::string::npos);
}

TEST_CASE("a repeated-container top-level child carrying only text is rejected "
          "on the named-space path",
          "[xml][repeated_container][malformed]")
{
    nucleus::schema_registry registry;
    REQUIRE(registry.attach(
            nucleus::repeated_element("node", nucleus::anchor::root())));
    REQUIRE(registry.attach(
            nucleus::element("port", nucleus::anchor::keyspace("node"))));
    auto source = test::xml_of("<cfg><node>oops</node></cfg>");
    source.with_space_name("cfg");
    source.apply_projection(registry.projection());
    const auto result = source.pull();
    REQUIRE_FALSE(result);
    CHECK(result.error().code == nucleus::errc::malformed_source);
    CHECK(result.error().message.find("character data") != std::string::npos);
}

TEST_CASE("emit refuses a value placed on a declared repeated container",
          "[xml][xml_emitter][malformed]")
{
    const nucleus::config_space space  = test::simple_cluster_space();
    const nucleus::config       config = test::checked_config(
            {{"cluster/node[0]", "oops"}});
    std::ostringstream output;
    const auto         result = nucleus::xml::emit_document(config, space, output);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == nucleus::errc::malformed_source);
    CHECK(result.error().message.find("cluster/node") != std::string::npos);
    CHECK(output.str().empty());
}

TEST_CASE("a declared repeated container carrying only whitespace CDATA is "
          "rejected on read",
          "[xml][repeated_container][malformed]")
{
    const nucleus::config_space space  = test::cluster_space();
    const auto                  loaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(
                    "<cluster><node><![CDATA[ ]]></node>"
                    "<node><port>90</port></node></cluster>"));
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::malformed_source);
    CHECK(loaded.error().message.find("character data") != std::string::npos);
}
