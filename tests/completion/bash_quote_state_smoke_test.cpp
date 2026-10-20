#include "completion/bash_test_support.h"

#include "nucleus/completion/bash_line_form.h"
#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <tuple>
#include <string>
#include <vector>
#include <utility>

using nucleus::anchor;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::bash_test::bash_available;
using nucleus::bash_test::check_rows;
using nucleus::bash_test::delimiter_registry;
using nucleus::bash_test::print_inserted;
using nucleus::bash_test::run_bash;
using nucleus::bash_test::run_history_expanded;
using nucleus::bash_test::run_seeded;
using nucleus::bash_test::shell_quoted;
using nucleus::completion_result_test::script_of;
using nucleus::detail::bash_line_word;
using nucleus::detail::bash_single_quoted_line_word;

namespace {

std::string shared_script()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("app", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("mode", anchor::keyspace(key_path::parse("app").value()), {"it's", "it'd", "a!b", "a!c"})));
    return script_of(generate_completion(shell::bash, reg, "myapp"));
}

std::string hostile_script()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("app", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("mode", anchor::keyspace(key_path::parse("app").value()),
                                             {"x;touch PWN_SEMI", "w\"$(touch PWN_DQ)\"y", "q\"", "$(touch PWN_A)", "a!b", "b!", "m;touch MARK_MID #", "\xc3\xa9;touch MARK_MB #", "only one"})));
    return script_of(generate_completion(shell::bash, reg, "myapp"));
}

const std::string cursor_drive = R"(at_cursor() { COMP_LINE="$2$3"; COMP_POINT=${#2}; local probe="$1"
    shift 3; COMP_WORDS=(myapp "$@"); COMP_CWORD=$#; "$probe"; }
replied() { _myapp_complete; printf '[%s]' "${COMPREPLY[@]}"; echo; }
tracked() { _myapp_quote "${COMP_WORDS[COMP_CWORD]}"; printf '[%s]\n' "$quote"; })";
}

TEST_CASE("a value typed inside a closed quote completes in the spelling of no open quote", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const std::string app  = hostile_script();
    const std::string semi = "<--app-mode=x;touch PWN_SEMI>\n";
    check_rows({
            {app, {"--app-mode", "=", "'x'"}, "--app-mode=", "", semi},
            {app, {"--app-mode", "=", "\"x\""}, "--app-mode=", "", semi},
            {app, {"--app-mode='x'"}, "--app-mode=", "", semi},
    });
}

TEST_CASE("a value completes in the spelling of the quote the typed word leaves open", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const std::string app = hostile_script();
    check_rows({
            {app, {"--app-mode", "=", "'w'\""}, "--app-mode='w'\"", "\"", "<--app-mode=ww\"$(touch PWN_DQ)\"y>\n"},
            {app, {"--app-mode", "=", "\"q\\\""}, "--app-mode=\"", "\"", "<--app-mode=q\">\n"},
    });
}

TEST_CASE("a flag typed inside closed quotes completes in the bare spelling of a hostile delimiter", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const auto delimiter = nucleus::cli_delimiter::parse("$(touch PWN_DELIM)");
    REQUIRE(delimiter);
    const std::string delimited = script_of(generate_completion(shell::bash, delimiter_registry(), "myapp", *delimiter));
    const std::string d         = "$(touch PWN_DELIM)";
    const std::string node      = "--cluster" + d + "node";
    const std::string flags     = "<" + node + ">\n<" + node + d + "port>\n<" + node + d + "*" + d + "port>\n<--cluster" + d + "port>\n";
    check_rows({
            {delimited, {"'--cluster$'"}, "", "", flags},
            {delimited, {"\"--cluster\\$\""}, "", "", flags},
    });
}

TEST_CASE("a value typed after an ANSI-C or locale quote takes the bare spelling, inert there", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const std::string app = hostile_script();
    check_rows({
            {app, {"--app-mode", "=", "$'"}, "--app-mode=$'", "'", "<--app-mode=\\$\\(touch\\ PWN_A\\)>\n"},
            {app, {"--app-mode", "=", "$\""}, "--app-mode=$\"", "\"", "<--app-mode=$\\(touch\\ PWN_A\\)>\n"},
    });
}

TEST_CASE("an inserted `!` survives the history expansion of the line it is typed into", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const std::string app = hostile_script();
    for(const auto &[typed, value] : {std::pair<std::string, std::string>{"\"a", "a!b"}, {"\"b!", "b!"}})
    {
        DYNAMIC_SECTION(typed)
        {
            const std::string lines = run_seeded(app, print_inserted({"--app-mode", "=", typed}, "--app-mode=\"", "\""), {});
            REQUIRE(run_history_expanded(lines) == "<--app-mode=" + value + ">\n");
        }
    }
}

TEST_CASE("a value shared through a reopened quote completes on the first TAB and offers nothing past the reopened quote", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const std::string app       = shared_script();
    const std::string first_tab = "COMP_WORDS=(myapp --app-mode = \"'i\")\nCOMP_CWORD=3\n_myapp_complete\nprintf '[%s]' \"${COMPREPLY[@]}\"; echo\n";
    REQUIRE(run_bash(app + first_tab) == "[it'\\''s][it'\\''d]\n");
    // bash matches nothing for a word that closes and reopens a quote, so no value is offered there.
    check_rows({
            {app, {"--app-mode", "=", "'it'\\''s"}, "--app-mode='it'\\''", "'", ""},
            {app, {"--app-mode", "=", "\"a\"\\!\"b"}, "--app-mode=\"a\"\\!\"", "\"", ""},
            {app, {"--app-mode", "=", "'it'\"'\"'s"}, "--app-mode='it'\"'\"'", "'", ""},
            {app, {"--app-mode", "=", "\"it'\"'s"}, "--app-mode=\"it'\"'", "'", ""},
    });
}

TEST_CASE("a reply typed with a quote or backslash right of the cursor takes the bare spelling", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const auto delimiter = nucleus::cli_delimiter::parse("$(touch PWN_DELIM)");
    REQUIRE(delimiter);
    const std::string delimited = script_of(generate_completion(shell::bash, delimiter_registry(), "myapp", *delimiter));
    const std::string app = hostile_script(), d = "$(touch PWN_DELIM)", mid = "m;touch MARK_MID #";
    const std::string e = "\xc3\xa9", k13 = e + e + e + e + e + e + e + e + e + e + e + e + e, wide = "myapp " + k13 + " --app-mode=";

    const std::vector<std::tuple<std::string, std::string, std::vector<std::string>>> rows = {
            {app, "[" + bash_line_word(mid) + "]\n", {"replied", "myapp --app-mode=m", "'", "--app-mode", "=", "m'"}},
            {app, "[" + bash_single_quoted_line_word(mid) + "]\n", {"replied", "myapp --app-mode=m'", "", "--app-mode", "=", "m'"}},
            {app, "[" + bash_line_word(mid) + "]\n", {"replied", wide + "m", "'", k13, "--app-mode", "=", "m'"}},
            {app, "[" + bash_single_quoted_line_word("only one") + "]\n", {"replied", wide + "'o", "", k13, "--app-mode", "=", "'o"}},
            {app, "[" + bash_line_word(e + ";touch MARK_MB #") + "]\n", {"replied", "myapp --app-mode=" + e, "'", "--app-mode", "=", e + "'"}},
            {delimited, "[" + bash_line_word("--cluster" + d + "port") + "]\n", {"replied", "myapp --cluster" + d + "p", "'", "--cluster" + d + "p'"}},
            {app, "[]\n", {"tracked", "myapp --app-mode=m", " x'", "--app-mode", "=", "m", "x'"}},
            {app, "[']\n", {"tracked", "myapp --app-mode='o", " and more", "--app-mode", "=", "'o and more"}},
    };
    for(const auto &[script, expected, call] : rows)
    {
        std::string drive = script + cursor_drive + "\nat_cursor";
        for(const std::string &arg : call)
            drive += " " + shell_quoted(arg);
        CAPTURE(call[1], call[2]);
        CHECK(run_bash(drive + "\n") == expected);
    }
}
