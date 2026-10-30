#include "completion/bash_test_support.h"

#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

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

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

std::string script(const schema_registry &reg, const nucleus::cli_delimiter &delimiter = {})
{
    const auto completion = generate_completion(shell::bash, reg, "myapp", delimiter);
    REQUIRE(completion);
    return *completion;
}

schema_registry logging_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("logging", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("logging")), {"has space", "debug"})));
    return reg;
}

schema_registry breaks_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("app", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("mode", anchor::keyspace(path_of("app")), {"a#b:c", "~r:x", "u^v:w", "user@host", "k=v"})));
    return reg;
}

schema_registry values_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("cluster/node")),
                                             {"has space", "x;y", "in$(touch${IFS}PWN_VAL)fo", "*", "~root", "co:lon"})));
    return reg;
}
}

TEST_CASE("every inserted bash candidate parses back to its own text", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const auto        delimiter = nucleus::cli_delimiter::parse("$(touch PWN_DELIM)");
    const std::string logging   = script(logging_registry());
    const std::string values    = script(values_registry());
    REQUIRE(delimiter);
    const std::string delimited = script(delimiter_registry(), *delimiter);
    const std::string d         = "$(touch PWN_DELIM)";
    const std::string node      = "--cluster" + d + "node";

    std::vector<insertion_row> rows{
            {logging, {"--logging-level", "=", "h"}, "--logging-level=", "", "<--logging-level=has space>\n"},
            {values, {"--cluster-node-"}, "", "", "<--cluster-node-level>\n<--cluster-node-*-level>\n"},
            {delimited, {"--cluster\\$"}, "", "", "<" + node + ">\n<" + node + d + "port>\n<" + node + d + "*" + d + "port>\n<--cluster" + d + "port>\n"},
            {delimited, {"--cluster\\$\\(touch\\ PWN_DELIM\\)node\\$\\(touch\\ PWN_DELIM\\)3"}, "", "", "<" + node + d + "3" + d + "port>\n"},
    };
    const std::vector<std::pair<std::string, std::string>> typed{
            {"h", "has space"},
            {"x", "x;y"},
            {"in", "in$(touch${IFS}PWN_VAL)fo"},
            {"has\\ s", "has space"},
            {"\\~", "~root"},
            {"~", "~root"},
            {"\\*", "*"},
            {"co", "co:lon"},
    };
    for(const std::string flag : {"--cluster-node-level", "--cluster-node-2-level"})
    {
        for(const auto &[prefix, value] : typed)
            rows.push_back({values, {flag, "=", prefix}, flag + "=", "", "<" + flag + "=" + value + ">\n"});
    }
    check_rows(rows);
}

TEST_CASE("a value typed past a word-break byte is never inserted twice", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const std::string values = script(values_registry());
    const std::string breaks = script(breaks_registry());
    const std::string flag   = "--cluster-node-level";
    const std::string plain  = nucleus::bash_emitter{}.emit({"myapp", {{"--v", "d", {"co:lon", "k=v"}, {}}}});
    // bash splits the value at an unquoted break byte and nothing re-joins it, so no value is offered.
    std::vector<insertion_row> rows{
            {values, {flag, "=", "co", ":"}, flag + "=co", "", ""},
            {values, {flag, "=", "co", ":", "l"}, flag + "=co:", "", ""},
            {values, {"--cluster-node-2-level", "=", "co", ":"}, "--cluster-node-2-level=co", "", ""},
            {values, {flag, "=", "'co:"}, flag + "='", "'", "<" + flag + "=co:lon>\n"},
            {breaks, {"--app-mode", "=", "user", "@", "h"}, "--app-mode=user@", "", ""},
            {breaks, {"--app-mode", "=", "k", "="}, "--app-mode=k", "", ""},
            {plain, {"--v", "=", "k", "="}, "--v=k", "", ""},
            {plain, {"--v", "=", "k"}, "--v=", "", "<--v=k=v>\n"},
    };
    for(const std::string typed : {"a#b", "a\\#b", "~r", "u^v"})
        rows.push_back({breaks, {"--app-mode", "=", typed, ":"}, "--app-mode=" + typed, "", ""});
    check_rows(rows);
    const std::string unbroken = "COMP_WORDBREAKS=${COMP_WORDBREAKS//:/}\n";
    REQUIRE(run_seeded(values, unbroken + insert_and_parse({flag, "=", "co:"}, flag + "=", ""), {}) == "<" + flag + "=co:lon>\n");
    REQUIRE(run_seeded(breaks, unbroken + insert_and_parse({"--app-mode", "=", "a#b:"}, "--app-mode=", ""), {}) == "<--app-mode=a#b:c>\n");
}

TEST_CASE("a word ending inside a quote opened after other text is offered nothing by any script", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const std::string plain  = nucleus::bash_emitter{}.emit({"myapp", {{"--app-mode", "", {"debug", "info"}, {}}}});
    const std::string quoted = nucleus::bash_emitter{}.emit({"myapp", {{"--app-mode", "", {"has space", "a!c"}, {}}}});
    check_rows({
            {plain, {"--app-mode", "=", "d"}, "--app-mode=", "", "<--app-mode=debug>\n"},
            {plain, {"--app-mode", "=", "'d''e"}, "--app-mode='d''", "'", ""},
            {plain, {"--app-mode", "=", "\"d\"\"e"}, "--app-mode=\"d\"\"", "\"", ""},
            {plain, {"'--app-m''o"}, "'--app-m''", "'", ""},
            {plain, {"--app-mode", "=", "d'e"}, "--app-mode=d'", "'", ""},
            {plain, {"--app-m'od"}, "--app-m'", "'", ""},
            {quoted, {"--app-mode", "=", "'h"}, "--app-mode='", "'", "<--app-mode=has space>\n"},
            {quoted, {"--app-mode", "=", "h'a"}, "--app-mode=h'", "'", ""},
            {quoted, {"--app-mode", "=", "a\\!'c"}, "--app-mode=a\\!'", "'", ""},
            {quoted, {"--app-m'o"}, "--app-m'", "'", ""},
    });
}

TEST_CASE("every byte but a newline round-trips through the inserted line", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

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
    const std::string drive      = insert_and_parse({"--v", "="}, "--v=", "");
    REQUIRE(run_seeded(completion, drive, {}) == expected);
    REQUIRE(run_seeded("export LC_ALL=C\n" + completion, drive, {}) == expected);
}
