#include "support/builder_result_test_support.h"
#include "support/completion_result_test_support.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/config_space.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using nucleus::completion_result_test::script_of;

namespace {

nucleus::key_path path_of(const char *text) { return nucleus::key_path::parse(text).value(); }

}

TEST_CASE("the facade generates --help text with description, values and required marker",
          "[facade][help]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::described(
        nucleus::enum_element("level", nucleus::anchor::keyspace(path_of("logging")),
                              {"debug", "info", "warn", "error"}),
        "set the logging level")));
    REQUIRE(engine.register_element(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::described(
        nucleus::required_element("host", nucleus::anchor::keyspace(path_of("server"))),
        "the address to bind")));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    const std::string help = space.generate_help("mytool");

    // The flag line carries its schema description -- the single source both the
    // completions and the help text read.
    REQUIRE(help.find("--logging-level") != std::string::npos);
    REQUIRE(help.find("set the logging level") != std::string::npos);
    // The allowed-values list is projected onto the same line.
    REQUIRE(help.find("[values: debug, info, warn, error]") != std::string::npos);
    // A required element gets a marker the completion model cannot supply.
    REQUIRE(help.find("--server-host") != std::string::npos);
    REQUIRE(help.find("the address to bind") != std::string::npos);
    REQUIRE(help.find("(required)") != std::string::npos);
    // Lines are grouped by the top-level keyspace segment.
    REQUIRE(help.find("logging:") != std::string::npos);
    REQUIRE(help.find("server:") != std::string::npos);
}

TEST_CASE("the facade --help lists a bare path-tagged flag alongside typed elements",
          "[facade][help]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    // A path-tagged registration is a recognized flag carrying no typed metadata;
    // it must still appear in --help -- the same surface the completions project,
    // so the two never disagree on which flags exist.
    REQUIRE(engine.register_schema("logging/verbose"));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    const std::string help = space.generate_help("mytool");
    const std::string completion = script_of(space.generate_completion(nucleus::shell::bash, "mytool"));

    REQUIRE(help.find("--logging") != std::string::npos);
    REQUIRE(help.find("--logging-verbose") != std::string::npos);
    REQUIRE(completion.find("--logging-verbose") != std::string::npos);
}
