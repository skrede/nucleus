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

    const std::string bash = space.generate_completion(nucleus::shell::bash, "mytool").value();

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

    const std::string zsh = space.generate_completion(nucleus::shell::zsh, "mytool").value();

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

TEST_CASE("the facade --help lists a bare path-tagged flag alongside typed elements",
          "[facade][help]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    // A path-tagged registration is a recognized flag carrying no typed metadata;
    // it must still appear in --help -- the same surface the completions project,
    // so the two never disagree on which flags exist.
    REQUIRE(engine.register_schema("logging/verbose"));
    nucleus::config_space space = engine.build();

    const std::string help = space.generate_help("mytool");
    const std::string completion = space.generate_completion(nucleus::shell::bash, "mytool").value();

    REQUIRE(help.find("--logging") != std::string::npos);
    REQUIRE(help.find("--logging-verbose") != std::string::npos);
    REQUIRE(completion.find("--logging-verbose") != std::string::npos);
}

TEST_CASE("a program name carrying a newline is refused instead of emitted as script text",
          "[facade][completion]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    nucleus::config_space space = engine.build();

    const auto refused = space.generate_completion(nucleus::shell::bash, "my\ntool");

    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().code == nucleus::errc::malformed_source);
    // The token is quoted in its escaped form, so the diagnostic cannot itself
    // carry the line break it reports.
    REQUIRE(refused.error().message.find("my\\ntool") != std::string::npos);
    REQUIRE(refused.error().message.find('\n') == std::string::npos);
}

TEST_CASE("a program name carrying a shell metacharacter is refused for both shells",
          "[facade][completion]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    nucleus::config_space space = engine.build();

    const auto bash = space.generate_completion(nucleus::shell::bash, "x; rm -rf ~");
    const auto zsh  = space.generate_completion(nucleus::shell::zsh, "x; rm -rf ~");

    REQUIRE_FALSE(bash);
    REQUIRE(bash.error().code == nucleus::errc::malformed_source);
    REQUIRE(bash.error().message.find("x; rm -rf ~") != std::string::npos);
    REQUIRE_FALSE(zsh);
    REQUIRE(zsh.error().code == nucleus::errc::malformed_source);
}

TEST_CASE("a plain program name still yields a script for both shells",
          "[facade][completion]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    nucleus::config_space space = engine.build();

    const auto bash = space.generate_completion(nucleus::shell::bash, "my-tool.v2");
    const auto zsh  = space.generate_completion(nucleus::shell::zsh, "my-tool.v2");

    REQUIRE(bash);
    // The last line is shell command position -- where an unquoted token that was
    // not a bare word would open a statement of its own.
    REQUIRE(bash.value().ends_with("complete -F _my_tool_v2_complete my-tool.v2\n"));
    REQUIRE(bash.value().find("--logging") != std::string::npos);
    REQUIRE(zsh);
    REQUIRE(zsh.value().starts_with("#compdef my-tool.v2\n"));
}
