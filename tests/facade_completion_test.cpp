#include "nucleus/config_space.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Completion reached THROUGH the public facade only: a host builds its schema via
// register_element/enum_element and calls config_space::generate_completion,
// never touching the internal schema registry. This proves the host-reachable path
// is wired end-to-end -- the gap that the free generator alone left open.

namespace {

nucleus::key_path path_of(const char *text) { return nucleus::key_path::parse(text).value(); }

} // namespace

TEST_CASE("the facade generates a bash completion script from the registered schema",
          "[facade][completion]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::enum_element(
        "level", nucleus::anchor::keyspace(path_of("logging")),
        {"debug", "info", "warn", "error"})));
    REQUIRE(engine.register_element(nucleus::element("plexus", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::element("port", nucleus::anchor::keyspace(path_of("plexus")))));
    nucleus::config_space space = engine.build();

    const std::string bash = space.generate_completion(nucleus::shell::bash, "mytool");

    REQUIRE(bash.find("complete -F _mytool_complete mytool") != std::string::npos);
    REQUIRE(bash.find("--logging") != std::string::npos);
    REQUIRE(bash.find("--logging-level") != std::string::npos);
    REQUIRE(bash.find("--plexus") != std::string::npos);
    REQUIRE(bash.find("--plexus-port") != std::string::npos);
    // The enum element's declared value set becomes that flag's candidates.
    REQUIRE(bash.find("'debug info warn error'") != std::string::npos);
}

TEST_CASE("the facade generates a zsh completion script from the registered schema",
          "[facade][completion]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::enum_element(
        "level", nucleus::anchor::keyspace(path_of("logging")),
        {"debug", "info", "warn", "error"})));
    nucleus::config_space space = engine.build();

    const std::string zsh = space.generate_completion(nucleus::shell::zsh, "mytool");

    REQUIRE(zsh.find("#compdef mytool") != std::string::npos);
    REQUIRE(zsh.find("_arguments -s") != std::string::npos);
    REQUIRE(zsh.find("--logging-level") != std::string::npos);
    REQUIRE(zsh.find("(debug info warn error)") != std::string::npos);
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
    nucleus::config_space space = engine.build();

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
