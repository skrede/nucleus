#include "nucleus/configuration_space.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Completion reached THROUGH the public facade only: a host builds its schema via
// register_element/enum_element and calls configuration_space::generate_completion,
// never touching the internal schema registry. This proves the host-reachable path
// is wired end-to-end -- the gap that the free generator alone left open.

namespace {

nucleus::key_path path_of(const char *text) { return nucleus::key_path::parse(text).value(); }

} // namespace

TEST_CASE("the facade generates a bash completion script from the registered schema",
          "[facade][completion]")
{
    nucleus::configuration_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::enum_element(
        "level", nucleus::anchor::keyspace(path_of("logging")),
        {"debug", "info", "warn", "error"})));
    REQUIRE(engine.register_element(nucleus::element("plexus", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::element("port", nucleus::anchor::keyspace(path_of("plexus")))));
    nucleus::configuration_space space = engine.build();

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
    nucleus::configuration_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::enum_element(
        "level", nucleus::anchor::keyspace(path_of("logging")),
        {"debug", "info", "warn", "error"})));
    nucleus::configuration_space space = engine.build();

    const std::string zsh = space.generate_completion(nucleus::shell::zsh, "mytool");

    REQUIRE(zsh.find("#compdef mytool") != std::string::npos);
    REQUIRE(zsh.find("_arguments -s") != std::string::npos);
    REQUIRE(zsh.find("--logging-level") != std::string::npos);
    REQUIRE(zsh.find("(debug info warn error)") != std::string::npos);
}
