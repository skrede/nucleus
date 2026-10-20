#include "completion/bash_test_support.h"

#include "nucleus/completion/bash_emitter.h"
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
#include <utility>

using nucleus::anchor;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::bash_test::bash_available;
using nucleus::bash_test::check_rows;
using nucleus::bash_test::delimiter_registry;
using nucleus::bash_test::insert_and_parse;
using nucleus::bash_test::insertion_row;
using nucleus::bash_test::run_seeded;
using nucleus::bash_test::shell_quoted;
using nucleus::completion_result_test::script_of;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

std::string script(const schema_registry &reg, const nucleus::cli_delimiter &delimiter = {})
{
    return script_of(generate_completion(shell::bash, reg, "myapp", delimiter));
}

schema_registry quoted_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("app", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("mode", anchor::keyspace(path_of("app")),
                                             {"only one", "has space", "has spice", "it's", "end'", "say\"hi\"", "q\"", "a!b", "b!", "back\\slash",
                                              "z'$(touch PWN_SQ)'b", "w\"$(touch PWN_DQ)\"y", "k`touch PWN_BQ`k", "p$(touch PWN_DS)"})));
    return reg;
}

schema_registry level_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("cluster/node")), {"has space", "in$(touch${IFS}PWN_VAL)fo"})));
    return reg;
}

schema_registry logging_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("logging", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("logging")), {"has space", "debug"})));
    return reg;
}

std::string raw_replies(const std::vector<std::string> &words)
{
    std::string out = "\nCOMP_WORDS=(myapp";
    for(const std::string &word : words)
        out += " " + shell_quoted(word);
    return out + ")\nCOMP_CWORD=$((${#COMP_WORDS[@]} - 1))\n_myapp_complete\nprintf '[%s]' \"${COMPREPLY[@]}\"\necho\n";
}
}

TEST_CASE("a value typed after an opening quote inserts as its own text", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const std::string                                      app = script(quoted_registry());
    const std::vector<std::pair<std::string, std::string>> typed{
            {"o", "only one"},
            {"i", "it's"},
            {"e", "end'"},
            {"s", "say\"hi\""},
            {"q", "q\""},
            {"a", "a!b"},
            {"b!", "b!"},
            {"ba", "back\\slash"},
            {"z", "z'$(touch PWN_SQ)'b"},
            {"w", "w\"$(touch PWN_DQ)\"y"},
            {"k", "k`touch PWN_BQ`k"},
            {"p", "p$(touch PWN_DS)"},
    };
    std::vector<insertion_row> rows;
    for(const std::string quote : {"'", "\""})
    {
        for(const auto &[prefix, value] : typed)
            rows.push_back({app, {"--app-mode", "=", quote + prefix}, "--app-mode=" + quote, quote, "<--app-mode=" + value + ">\n"});
    }
    check_rows(rows);
}

TEST_CASE("a quoted prefix shared by several candidates is left as typed", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const std::string app = script(quoted_registry());
    for(const std::string quote : {"'", "\""})
    {
        const std::vector<std::string> words{"--app-mode", "=", quote + "has sp"};
        REQUIRE(run_seeded(app, raw_replies(words), {}) == "[has space][has spice]\n");
        REQUIRE(run_seeded(app, insert_and_parse(words, "--app-mode=" + quote, quote), {}) == "<--app-mode=has space>\n<--app-mode=has spice>\n");
    }
}

TEST_CASE("ordinal and static flags and flag replies complete after either quote", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const auto delimiter = nucleus::cli_delimiter::parse("$(touch PWN_DELIM)");
    REQUIRE(delimiter);
    const std::string          levels    = script(level_registry());
    const std::string          delimited = script(delimiter_registry(), *delimiter);
    const std::string          port      = "--cluster$(touch PWN_DELIM)port";
    std::vector<insertion_row> rows{
            {script(logging_registry()), {"--logging-level", "=", "'h"}, "--logging-level='", "'", "<--logging-level=has space>\n"},
            {delimited, {"'--cluster$(touch PWN_DELIM)p"}, "'", "'", "<" + port + ">\n"},
            {delimited, {"\"--cluster\\$(touch PWN_DELIM)p"}, "\"", "\"", "<" + port + ">\n"},
    };
    for(const std::string quote : {"'", "\""})
    {
        rows.push_back({levels, {quote + "--cluster-node-*-l"}, quote, quote, "<--cluster-node-*-level>\n"});
        for(const std::string flag : {"--cluster-node-2-level", "--cluster-node-level"})
        {
            rows.push_back({levels, {flag, "=", quote + "h"}, flag + "=" + quote, quote, "<" + flag + "=has space>\n"});
            rows.push_back({levels, {flag, "=", quote + "in"}, flag + "=" + quote, quote, "<" + flag + "=in$(touch${IFS}PWN_VAL)fo>\n"});
        }
    }
    check_rows(rows);
}

TEST_CASE("every byte but a newline round-trips through a quoted inserted line", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    nucleus::completion_model model{"myapp", {{"--v", "d", {}}}};
    std::string               expected;
    for(std::uint32_t byte = 0x01; byte <= 0xff; ++byte)
    {
        if(byte == '\n')
            continue;
        const std::string value(1, static_cast<char>(byte));
        model.options.front().values.push_back(value);
        expected += "<--v=" + value + ">\n";
    }
    const std::string completion = nucleus::bash_emitter{}.emit(model);
    for(const std::string quote : {"'", "\""})
    {
        const std::string drive = insert_and_parse({"--v", "=", quote}, "--v=" + quote, quote);
        REQUIRE(run_seeded(completion, drive, {}) == expected);
        REQUIRE(run_seeded("export LC_ALL=C\n" + completion, drive, {}) == expected);
    }
}
