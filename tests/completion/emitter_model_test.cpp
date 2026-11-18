#include "completion/bash_test_support.h"

#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/completion_model.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <filesystem>

using nucleus::bash_emitter;
using nucleus::completion_model;
using nucleus::completion_option;
using nucleus::zsh_emitter;
using nucleus::bash_test::bash_available;
using nucleus::bash_test::payload_check;
using nucleus::bash_test::run_bash;
using nucleus::bash_test::run_seeded;
using nucleus::bash_test::scratch_tree;
using nucleus::bash_test::shell_quoted;

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
    REQUIRE(zsh.find("_nucleus_myapp_ordinal()\n") != std::string::npos);
    REQUIRE(zsh.find("    _nucleus_myapp_ordinal \"$ordinal_word\" '[]' '--a' '--a' 'b' 'b'\n") != std::string::npos);
}

TEST_CASE("a flag zsh quotes is offered only from zsh 5.9", "[completion][model]")
{
    const std::string zsh = zsh_emitter{}.emit({"myapp", {{"--a", "d", {}, {}}, {"--b$c", "d", {}, {}}}});
    REQUIRE(zsh.find("do not edit by hand.\nautoload -Uz is-at-least\n_nucleus_myapp()\n") != std::string::npos);
    REQUIRE(zsh.find("is-at-least\n") == zsh.rfind("is-at-least\n"));
    REQUIRE(zsh.find("    if (( inserts_quoted )); then\n        quoted_specs=(\n            '--b\\$c[d]'\n        )\n    fi\n") != std::string::npos);
    REQUIRE(zsh.find("--b\\$c") == zsh.rfind("--b\\$c"));
    REQUIRE(zsh.find("    _arguments -s \\\n        '--a[d]' \\\n        \"${quoted_specs[@]}\"\n}\n") != std::string::npos);
    REQUIRE(zsh_emitter{}.emit({"myapp", {{"--b$c", "d", {}, {}}}}).find("    _arguments -s \\\n        \"${quoted_specs[@]}\"\n}\n") != std::string::npos);
    REQUIRE(zsh_emitter{}.emit({"myapp", {{"--a", "d", {}, {}}}}).find("is-at-least") == std::string::npos);
}

TEST_CASE("a hostile program name reaches the bash script only as inert text", "[completion][model][smoke]")
{
    const std::string bash = bash_emitter{}.emit(model_of({"--w", "", {}, {}}, hostile_prog));
    REQUIRE(bash.starts_with("# bash completion for x$(touch PWN_PROG);y\\ntouch PWN_LINE\n#"));
    REQUIRE(bash.ends_with(" x\\$\\(touch\\ PWN_PROG\\)\\;y\\\ntouch\\ PWN_LINE; fi\n"));

    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");
    // The probe reads the status because bash 5.0 and 5.1 print the registered name bare and 5.2 quotes it. The
    // emitter writes the newline as a line continuation, so the name bash registers has none.
    const std::string probe = "\ncomplete -p " + shell_quoted("x$(touch PWN_PROG);ytouch PWN_LINE") + " >/dev/null && echo registered\n";
    REQUIRE(run_seeded(bash, probe + std::string(payload_check) + "echo sourced\n", {}) == "registered\nsourced\n");
}

TEST_CASE("the generated bash script registers its completion on a supported bash", "[completion][model][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");
    const std::string bash = bash_emitter{}.emit(model_of({"--w", "", {}, {}}));
    REQUIRE(run_bash(bash + "complete -p myapp\n") == "complete -F _myapp_complete myapp\n");
}

TEST_CASE("a hostile program name reaches the zsh script only as inert text", "[completion][model]")
{
    const std::string zsh   = zsh_emitter{}.emit(model_of({"--w", "", {}, {}}, hostile_prog));
    const std::string plain = zsh_emitter{}.emit(model_of({"--w", "", {}, {}}));
    const std::string shown = "x$(touch PWN_PROG);y\\ntouch PWN_LINE\n";
    REQUIRE(zsh.starts_with("#compdef " + shown + "# zsh completion for " + shown + "#"));
    const std::string fn = "_nucleus_x__touch_PWN_PROG__y_touch_PWN_LINE";
    REQUIRE(zsh.ends_with("}\nif [ \"$funcstack[1]\" = \"" + fn + "\" ] && [ \"${zsh_eval_context[-1]}\" != file ]; then " + fn + " \"$@\"; else compdef " + fn + " x\\$\\(touch\\ PWN_PROG\\)\\;y\\\ntouch\\ PWN_LINE; fi\n"));
    REQUIRE(std::ranges::count(zsh, '\n') == std::ranges::count(plain, '\n') + 1);
}

TEST_CASE("a scratch tree is removed when a failure unwinds past it", "[completion][support]")
{
    std::filesystem::path recorded;
    const auto            failing = [&recorded]
    {
        const scratch_tree tree("nucleus_completion_unwind");
        recorded = tree.path();
        std::ofstream(tree.path() / "held") << "held";
        throw std::runtime_error("drive failed");
    };
    CHECK_THROWS_AS(failing(), std::runtime_error);
    REQUIRE_FALSE(recorded.empty());
    REQUIRE_FALSE(std::filesystem::exists(recorded));
}

// Passes only while run_bash fails a drive on its exit status: the output is discarded, so that check is the one
// assertion it reaches. The control below shares the drive, so a setup failure shows there.
TEST_CASE("a drive whose bash exits non-zero fails whatever it printed", "[completion][smoke][!shouldfail]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");
    run_bash("echo printed\nexit 3\n");
}

TEST_CASE("a drive whose bash exits zero returns what it printed", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");
    REQUIRE(run_bash("echo printed\n") == "printed\n");
}
