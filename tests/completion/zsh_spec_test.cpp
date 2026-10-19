#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/completion_model.h"
#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <algorithm>

// No zsh runs where this suite runs, so these pin the emitted text. What each assertion implies was
// measured under zsh 5.9: `_arguments` evals a `(...)` value group under `extendedglob nullglob`.

namespace {

bool is_c1_byte(char c)
{
    return static_cast<std::uint8_t>(c) >= 0x80 && static_cast<std::uint8_t>(c) <= 0x9f;
}

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
    const std::vector<std::string> prose{"caf\xc3\xa9 \xe2\x80\xa9", "\xe2\x9b\x80", "\xf0\x9f\x98\x80", "a\xc2\xa0z"};
    for(const std::string &text : prose)
    {
        CAPTURE(text);
        REQUIRE(emitted("--f", text, {}).find("[" + text + "]") != std::string::npos);
    }
}

TEST_CASE("a C1 control in a zsh description is rendered, not passed", "[completion]")
{
    nucleus::schema_registry reg;
    REQUIRE(reg.attach(nucleus::described(nucleus::element("o", nucleus::anchor::root()), std::string("a\xc2\x9b") + "31mz")));
    const nucleus::expected<std::string, nucleus::error> zsh = nucleus::generate_completion(nucleus::shell::zsh, reg, "myapp");
    REQUIRE(zsh);
    REQUIRE(zsh->find("[a\\\\xc2\\\\x9b31mz]") != std::string::npos);
    REQUIRE(zsh->find("\xc2\x9b") == std::string::npos);
    const std::vector<std::pair<std::string, std::string>> boundaries{
            {"\xc2\x80", "[a\\\\xc2\\\\x80z]"}, {"\xc2\x85", "[a\\\\xc2\\\\x85z]"}, {"\xc2\x9f", "[a\\\\xc2\\\\x9fz]"}};
    for(const auto &[control, rendered] : boundaries)
    {
        const std::string spec = emitted("--f", "a" + control + "z", {});
        CAPTURE(spec);
        REQUIRE(spec.find(rendered) != std::string::npos);
        REQUIRE(spec.find(control) == std::string::npos);
    }
}

TEST_CASE("a bare C1 byte in a zsh description is rendered and nothing else is", "[completion]")
{
    const std::vector<std::pair<std::string, std::string>> rows{
            {std::string("a\x9b") + "31mz", "[a\\\\x9b31mz]"},
            {"\x80lead", "[\\\\x80lead]"},
            {"trail\x9f", "[trail\\\\x9f]"},
            {"a\xe0\x9b\x80z", "[a\xe0\\\\x9b\\\\x80z]"},
            {"a\xed\xa0\x80z", "[a\xed\xa0\\\\x80z]"},
            {"a\xc2", "[a\xc2]"},
            {"a\xc2\x41", "[a\xc2\x41]"},
    };
    for(const auto &[description, rendered] : rows)
    {
        const std::string spec = emitted("--f", description, {});
        CAPTURE(spec);
        REQUIRE(spec.find(rendered) != std::string::npos);
        REQUIRE(std::ranges::none_of(spec, is_c1_byte));
    }
}
