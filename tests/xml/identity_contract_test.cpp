#include "identity/pkey_identity_test_support.h"

#include "nucleus/error.h"
#include "nucleus/config.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <sstream>
#include <utility>

namespace pkey_test = nucleus::pkey_test;

namespace {

nucleus::config config_of(std::map<std::string, std::string> values)
{
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

nucleus::load_result reload(const nucleus::config_space &space,
                            const std::string           &document)
{
    nucleus::load_options options;
    options.document_paths = {"emitted.xml"};
    options.make_document  = [&document](const std::string &)
    {
        return pkey_test::xml_of(document);
    };
    return nucleus::load_config(space, nucleus::source_stack{}, options);
}

}

TEST_CASE("present pkey renders once as an XML attribute",
          "[pkey_identity][xml]")
{
    const nucleus::config_space space  = pkey_test::cluster_space();
    const nucleus::load_result  loaded = pkey_test::load_doc(
            space, R"(<cluster><server name="web"><port>80</port></server></cluster>)");
    REQUIRE(loaded);
    auto emitted = nucleus::xml::render_document(loaded.value(), space);
    REQUIRE(emitted);
    REQUIRE(emitted->find("name=\"web\"") != std::string::npos);
    REQUIRE(emitted->find("<name>web</name>") == std::string::npos);
}

TEST_CASE("optional pkey may be absent from safe XML", "[pkey_identity][xml]")
{
    const nucleus::config_space space   = pkey_test::cluster_space();
    auto                        emitted = nucleus::xml::render_document(
            config_of({{"cluster/server/port", "80"}}), space);
    REQUIRE(emitted);
    REQUIRE(emitted->find("<port>80</port>") != std::string::npos);
    REQUIRE(emitted->find(" name=") == std::string::npos);
}

TEST_CASE("required pkey absence names the concrete XML path",
          "[pkey_identity][xml]")
{
    const nucleus::config_space space = pkey_test::cluster_space(true);
    std::ostringstream          output;
    auto                        result = nucleus::xml::emit_document(
            config_of({{"cluster/server/port", "80"}}), space, output);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(result.error().message.find("cluster/server/name") != std::string::npos);
    REQUIRE(output.str().empty());
}

TEST_CASE("multiple pkey values fail before XML delivery",
          "[pkey_identity][xml]")
{
    const nucleus::config_space space  = pkey_test::cluster_space();
    const nucleus::config       config = config_of({{"cluster/server/name[0]", "web"},
                                                    {"cluster/server/name[1]", "db"}});
    std::ostringstream          output;
    auto                        result = nucleus::xml::emit_document(config, space, output);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(result.error().message.find("cluster/server/name[0]") != std::string::npos);
    REQUIRE(output.str().empty());
}

TEST_CASE("pkey XML round trip preserves keys and values",
          "[pkey_identity][xml][round_trip]")
{
    const nucleus::config_space space = pkey_test::cluster_space();
    const nucleus::load_result  first = pkey_test::load_doc(
            space, R"(<cluster><server name="web"><port>80</port></server></cluster>)");
    REQUIRE(first);
    auto emitted = nucleus::xml::render_document(first.value(), space);
    REQUIRE(emitted);
    REQUIRE_FALSE(emitted->empty());
    const nucleus::load_result second = reload(space, emitted.value());
    REQUIRE(second);
    REQUIRE(second->keys() == first->keys());
    REQUIRE(second->get("cluster/server/name") == first->get("cluster/server/name"));
    REQUIRE(second->get("cluster/server/port") == first->get("cluster/server/port"));
}
