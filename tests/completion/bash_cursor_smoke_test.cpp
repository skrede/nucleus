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

#include <string>
#include <vector>
#include <cstdint>
#include <string_view>

using nucleus::anchor;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::bash_test::bash_available;
using nucleus::bash_test::delimiter_registry;
using nucleus::bash_test::run_bash;
using nucleus::bash_test::shell_quoted;
using nucleus::completion_result_test::script_of;
using nucleus::detail::bash_line_word;
using nucleus::detail::bash_single_quoted_line_word;

namespace {

std::string cursor_script()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("app", anchor::root())));
    const auto app = key_path::parse("app");
    REQUIRE(app);
    REQUIRE(reg.attach(nucleus::enum_element("mode", anchor::keyspace(*app), {"m;touch MARK_MID #", "\xc3\xa9;touch MARK_MB #", "only one"})));
    return script_of(generate_completion(shell::bash, reg, "myapp"));
}

struct cursor_row
{
    std::string              script;
    std::string              expected;
    std::string              probe;
    std::string              left;
    std::string              right;
    std::int32_t             cword;
    std::vector<std::string> words;
};

std::string repeated(std::string_view text, std::int32_t count)
{
    std::string out;
    for(std::int32_t i = 0; i < count; ++i)
        out += text;
    return out;
}

// COMP_POINT counts characters only under a UTF-8 locale, and C.UTF-8 is not known to exist on macOS.
#ifdef __APPLE__
constexpr std::string_view cursor_locale = "en_US.UTF-8";
#else
constexpr std::string_view cursor_locale = "C.UTF-8";
#endif

const std::string cursor_drive = R"(
at_cursor() { COMP_LINE="$2$3"; COMP_POINT=${#2}; COMP_CWORD=$4; local probe="$1"
    shift 4; COMP_WORDS=(myapp "$@"); "$probe"; }
replied() { _myapp_complete; printf '[%s]' "${COMPREPLY[@]}"; echo; }
tracked() { _myapp_quote "${COMP_WORDS[COMP_CWORD]}"; printf '[%s]\n' "$quote"; })";
}

TEST_CASE("a reply at the cursor is bare beside a quote or backslash and absent past a quote opened after other text", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    // A failed LC_ALL assignment leaves the locale in force, so C goes first and a missing locale cannot pass on a UTF-8 one.
    const std::string locale_line = "LC_ALL=C\nLC_ALL=" + std::string(cursor_locale) + "\n";
    INFO("the cursor rows count characters only under the locale " << cursor_locale);
    REQUIRE(run_bash(locale_line + "e=$'\\303\\251'; echo ${#e}\n") == "1\n");

    const auto delimiter = nucleus::cli_delimiter::parse("$(touch PWN_DELIM)");
    REQUIRE(delimiter);
    const std::string delimited = script_of(generate_completion(shell::bash, delimiter_registry(), "myapp", *delimiter));
    const std::string app       = cursor_script();
    const std::string cluster   = "--cluster$(touch PWN_DELIM)";
    const std::string e         = "\xc3\xa9";
    const std::string k13       = repeated(e, 13);
    const std::string wide      = "myapp " + k13 + " --app-mode=";

    const std::vector<cursor_row> rows = {
            {app, "[]\n", "replied", "myapp --app-mode=m", "'", 3, {"--app-mode", "=", "m'"}},
            {app, "[]\n", "replied", "myapp --app-mode=m'", "", 3, {"--app-mode", "=", "m'"}},
            {app, "[]\n", "replied", wide + "m", "'", 4, {k13, "--app-mode", "=", "m'"}},
            {app, "[" + bash_single_quoted_line_word("only one") + "]\n", "replied", wide + "'o", "", 4, {k13, "--app-mode", "=", "'o"}},
            {app, "[]\n", "replied", "myapp --app-mode=" + e, "'", 3, {"--app-mode", "=", e + "'"}},
            {delimited, "[]\n", "replied", "myapp " + cluster + "p", "'", 1, {cluster + "p'"}},
            {app, "[]\n", "tracked", "myapp --app-mode=\"o", " a\"'b", 3, {"--app-mode", "=", "\"o a\"'b"}},
            {app, "[']\n", "tracked", "myapp --app-mode='o", " and more", 3, {"--app-mode", "=", "'o and more"}},
            {delimited, "[" + bash_line_word(cluster + "port") + "]\n", "replied", "myapp ", "'" + cluster + "p", 1, {"'" + cluster + "p"}},
            {delimited, "[" + bash_line_word(cluster + "port") + "]\n", "replied", "myapp " + k13 + " ", "'" + cluster + "p", 2, {k13, "'" + cluster + "p"}},
    };
    for(const cursor_row &row : rows)
    {
        std::string drive = locale_line + row.script + cursor_drive + "\nat_cursor";
        for(const std::string &arg : {row.probe, row.left, row.right, std::to_string(row.cword)})
            drive += " " + shell_quoted(arg);
        for(const std::string &word : row.words)
            drive += " " + shell_quoted(word);
        CAPTURE(row.left, row.right);
        CHECK(run_bash(drive + "\n") == row.expected);
    }
}
