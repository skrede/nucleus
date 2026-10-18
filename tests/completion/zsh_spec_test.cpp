#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/completion_model.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// No zsh runs where this suite runs, so these pin the emitted text. What each assertion implies was
// measured under zsh 5.9: `_arguments` evals a `(...)` value group under `extendedglob nullglob`.

namespace {

std::string emitted(const std::string &flag, const std::string &description,
                    const std::vector<std::string> &values)
{
    return nucleus::zsh_emitter{}.emit({"myapp", {{flag, description, values}}});
}

}

TEST_CASE("a glob in a zsh value group is escaped so the eval offers it literally", "[completion]")
{
    const std::string zsh = emitted("--app-glob", "d", {"*", "decoy*", "[a-z]*"});
    REQUIRE(zsh.find(":value:(\\* decoy\\* \\[a-z\\]\\*)'") != std::string::npos);
}

TEST_CASE("a leading equals in a zsh value cannot reach EQUALS expansion", "[completion]")
{
    const std::string zsh = emitted("--app-pick", "d", {"=ls", "a=b"});
    REQUIRE(zsh.find(":value:(\\=ls a\\=b)'") != std::string::npos);
}

TEST_CASE("the ordinal wildcard in a zsh option name is escaped", "[completion]")
{
    REQUIRE(emitted("--node-*-port", "d", {}).find("'--node-\\*-port[d]'") != std::string::npos);
}

TEST_CASE("a byte zsh reads as an option-name suffix stays part of the name", "[completion]")
{
    REQUIRE(emitted("--a-", "d", {}).find("'--a\\-[d]'") != std::string::npos);
    REQUIRE(emitted("--a+", "d", {}).find("'--a\\+[d]'") != std::string::npos);
    REQUIRE(emitted("--a=", "d", {}).find("'--a\\=[d]'") != std::string::npos);
    REQUIRE(emitted("--a-b+c", "d", {}).find("'--a-b+c[d]'") != std::string::npos);
    REQUIRE(emitted("--a-", "d", {"v"}).find("'--a-=[d]:value:(v)'") != std::string::npos);
}

TEST_CASE("every bidirectional control in a zsh description is rendered, not passed", "[completion]")
{
    const std::vector<std::string> controls{"\xe2\x80\xaa", "\xe2\x80\xab", "\xe2\x80\xac",
                                            "\xe2\x80\xad", "\xe2\x80\xae", "\xe2\x81\xa6",
                                            "\xe2\x81\xa7", "\xe2\x81\xa8", "\xe2\x81\xa9"};
    for(const std::string &control : controls)
    {
        const std::string zsh = emitted("--f", "a" + control + "b", {});
        CAPTURE(zsh);
        REQUIRE(zsh.find(control) == std::string::npos);
        REQUIRE(zsh.find("[a\\\\xe2\\\\x8") != std::string::npos);
    }
    REQUIRE(emitted("--f", "safe\xe2\x80\xaexes", {}).find("[safe\\\\xe2\\\\x80\\\\xaexes]") != std::string::npos);
}

TEST_CASE("non-ASCII prose that is no bidi control survives a zsh description", "[completion]")
{
    REQUIRE(emitted("--f", "caf\xc3\xa9 \xe2\x80\xa9", {}).find("[caf\xc3\xa9 \xe2\x80\xa9]") != std::string::npos);
}
