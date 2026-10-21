#include "completion/bash_test_support.h"

#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
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
using nucleus::completion_result_test::script_of;

namespace {

std::string shared_script()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("app", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("mode", anchor::keyspace(key_path::parse("app").value()), {"it's", "it'd", "a!b", "a!c", "abc", "has space"})));
    return script_of(generate_completion(shell::bash, reg, "myapp"));
}

std::string hostile_script()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("app", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("mode", anchor::keyspace(key_path::parse("app").value()),
                                             {"x;touch PWN_SEMI", "w\"$(touch PWN_DQ)\"y", "q\"", "$(touch PWN_A)", "a!b", "b!"})));
    return script_of(generate_completion(shell::bash, reg, "myapp"));
}
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
            {app, {"--app-mode", "=", "'w"}, "--app-mode='", "'", "<--app-mode=w\"$(touch PWN_DQ)\"y>\n"},
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
    const std::string hostile   = hostile_script();
    const std::string first_tab = "COMP_WORDS=(myapp --app-mode = \"'i\")\nCOMP_CWORD=3\n_myapp_complete\nprintf '[%s]' \"${COMPREPLY[@]}\"; echo\n";
    REQUIRE(run_bash(app + first_tab) == "[it'\\''s][it'\\''d]\n");
    // compgen still matches a plain close-and-reopen, but readline would keep the text before the reopened quote, so
    // nothing is offered for a word that ends inside a quote it reopened.
    check_rows({
            {app, {"--app-mode", "=", "'it'\\''s"}, "--app-mode='it'\\''", "'", ""},
            {app, {"--app-mode", "=", "\"a\"\\!\"b"}, "--app-mode=\"a\"\\!\"", "\"", ""},
            {app, {"--app-mode", "=", "'it'\"'\"'s"}, "--app-mode='it'\"'\"'", "'", ""},
            {app, {"--app-mode", "=", "\"it'\"'s"}, "--app-mode=\"it'\"'", "'", ""},
            {app, {"--app-mode", "=", "'a''bc"}, "--app-mode='a''", "'", ""},
            {app, {"--app-mode", "=", "\"h\"\"as spa"}, "--app-mode=\"h\"\"", "\"", ""},
            {app, {"--app-mode", "=", "'h''as spa"}, "--app-mode='h''", "'", ""},
            {hostile, {"--app-mode", "=", "'w'\""}, "--app-mode='w'\"", "\"", ""},
            {app, {"--app-mode", "=", "'a''bc'"}, "--app-mode=", "", "<--app-mode=abc>\n"},
    });
}
