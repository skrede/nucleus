#include "completion/bash_test_support.h"

#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/completion_model.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>

using nucleus::bash_emitter;
using nucleus::completion_model;
using nucleus::completion_option;
using nucleus::zsh_emitter;
using nucleus::bash_test::bash_available;
using nucleus::bash_test::run_seeded;

namespace {

// Past the flag twice, out of order, repeated, off a `*`, and one past the end of the flag.
const std::vector<completion_option> invalid_slot_rows{
        {"--abc", "", {}, {5}},
        {"--abc", "", {}, {9}},
        {"--a*b*c", "", {}, {5, 3}},
        {"--a*b*c", "", {}, {3, 3}},
        {"--a*b*c", "", {}, {2, 1}},
        {"--a*b", "", {}, {1}},
        {"--a*", "", {}, {4}},
};

const std::string hostile_prog = "x$(touch PWN_PROG);y\ntouch PWN_LINE";

completion_option without_slots(completion_option option)
{
    option.ordinal_slots.clear();
    return option;
}

completion_model model_of(const completion_option &option, const std::string &prog = "myapp")
{
    return {prog, {option, {"--v", "d", {"a"}, {}}}};
}

std::vector<std::string> emissions(const completion_model &model)
{
    return {bash_emitter{}.emit(model), zsh_emitter{}.emit(model)};
}

}

TEST_CASE("an unusable ordinal slot list emits the option's literal flag", "[completion][model]")
{
    for(const completion_option &option : invalid_slot_rows)
    {
        DYNAMIC_SECTION(option.flag << " " << option.ordinal_slots.front())
        {
            REQUIRE(emissions(model_of(option)) == emissions(model_of(without_slots(option))));
        }
    }
}

TEST_CASE("a usable ordinal slot list still emits the walker", "[completion][model]")
{
    const std::string bash = bash_emitter{}.emit(model_of({"--a*b", "", {}, {3}}));
    REQUIRE(bash.find("_myapp_ordinal()\n") != std::string::npos);
    REQUIRE(bash.find("    _myapp_ordinal \"$1\" '--a' 'b' && printf") != std::string::npos);
    REQUIRE(bash.find("    '--a*b') printf '%s\\n' '--a\\*b' ;;\n") != std::string::npos);

    const std::string zsh = zsh_emitter{}.emit(model_of({"--a*b", "", {}, {3}}));
    REQUIRE(zsh.find("_myapp_ordinal()\n") != std::string::npos);
    REQUIRE(zsh.find("    _myapp_ordinal \"$ordinal_word\" '[]' '--a' '--a' 'b' 'b'\n") != std::string::npos);
}

TEST_CASE("a hostile program name reaches the bash script only as inert text", "[completion][model][smoke]")
{
    const std::string bash = bash_emitter{}.emit(model_of({"--w", "", {}, {}}, hostile_prog));
    REQUIRE(bash.starts_with("# bash completion for x$(touch PWN_PROG);y\\ntouch PWN_LINE\n#"));
    REQUIRE(bash.ends_with(" x\\$\\(touch\\ PWN_PROG\\)\\;y\\\ntouch\\ PWN_LINE; fi\n"));

    if(!bash_available())
        SKIP("bash not found on PATH -- sourcing the generated script needs it");
    const std::string drive = "\nfor f in PWN_*; do [ -e \"$f\" ] && echo \"PAYLOAD $f\"; done\necho sourced\n";
    REQUIRE(run_seeded(bash, drive, {}) == "sourced\n");
}

TEST_CASE("a hostile program name reaches the zsh script only as inert text", "[completion][model]")
{
    const std::string zsh   = zsh_emitter{}.emit(model_of({"--w", "", {}, {}}, hostile_prog));
    const std::string plain = zsh_emitter{}.emit(model_of({"--w", "", {}, {}}));
    const std::string shown = "x$(touch PWN_PROG);y\\ntouch PWN_LINE\n";
    REQUIRE(zsh.starts_with("#compdef " + shown + "# zsh completion for " + shown + "#"));
    REQUIRE(std::ranges::count(zsh, '\n') == std::ranges::count(plain, '\n'));
}
