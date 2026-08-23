#include "nucleus/config_space.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

// The program name reaches shell command position unquoted -- `complete -F fn <prog>`
// in bash, `#compdef <prog>` in zsh -- so a token that reads as an option or a path
// reference at that position is refused before any script text exists.

namespace {

nucleus::config_space one_element_space()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    return engine.build();
}

void require_refused(const nucleus::config_space &space, std::string_view prog)
{
    const auto bash = space.generate_completion(nucleus::shell::bash, prog);
    const auto zsh  = space.generate_completion(nucleus::shell::zsh, prog);
    REQUIRE_FALSE(bash);
    REQUIRE(bash.error().code == nucleus::errc::malformed_source);
    REQUIRE_FALSE(zsh);
    REQUIRE(zsh.error().code == nucleus::errc::malformed_source);
}

}

TEST_CASE("a program name opening with '-' is refused for both shells", "[facade][completion]")
{
    const nucleus::config_space space = one_element_space();

    // `complete -F fn -D` is accepted by bash and registers fn as the default
    // completion for every command that has none, rebinding the whole session.
    for(std::string_view prog : {"-", "--", "-D", "-o", "-rf", "--prefix"})
    {
        CAPTURE(prog);
        require_refused(space, prog);
    }
}

TEST_CASE("a program name that is a path reference is refused for both shells",
          "[facade][completion]")
{
    const nucleus::config_space space = one_element_space();

    for(std::string_view prog : {".", "..", ".hidden"})
    {
        CAPTURE(prog);
        require_refused(space, prog);
    }
}

TEST_CASE("a program name carrying a bidirectional override reaches the message escaped",
          "[facade][completion]")
{
    const nucleus::config_space space = one_element_space();

    // U+202E RIGHT-TO-LEFT OVERRIDE (CVE-2021-42574): quoted verbatim it would make
    // the diagnostic render the very token it refused in reversed order.
    const auto refused = space.generate_completion(nucleus::shell::bash, "safe\xe2\x80\xaexes");

    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().message.find("\\xe2\\x80\\xae") != std::string::npos);
    REQUIRE(refused.error().message.find('\xe2') == std::string::npos);
}

TEST_CASE("a program name carrying an eight-bit control byte reaches the message escaped",
          "[facade][completion]")
{
    const nucleus::config_space space = one_element_space();

    // 0x9b is CSI in an eight-bit-clean terminal, so an unescaped copy would open a
    // color escape sequence out of the diagnostic.
    std::string prog = "a";
    prog.push_back('\x9b');
    prog += "31mRED";

    const auto refused = space.generate_completion(nucleus::shell::zsh, prog);

    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().message.find("\\x9b31mRED") != std::string::npos);
    REQUIRE(refused.error().message.find('\x9b') == std::string::npos);
}

TEST_CASE("a program name opening with a letter, digit or underscore still yields a script",
          "[facade][completion]")
{
    const nucleus::config_space space = one_element_space();

    for(std::string_view prog : {"mytool", "2to3", "_tool", "my-tool.v2"})
    {
        CAPTURE(prog);
        REQUIRE(space.generate_completion(nucleus::shell::bash, prog));
        REQUIRE(space.generate_completion(nucleus::shell::zsh, prog));
    }
}
