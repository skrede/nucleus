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
using nucleus::bash_test::insert_and_parse;
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

schema_registry values_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("cluster/node")),
                                             {"has space", "x;y", "in$(touch${IFS}PWN_VAL)fo", "*", "~root", "co:lon"})));
    return reg;
}

schema_registry delimiter_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node")))));
    return reg;
}

struct insertion_row
{
    std::string              script;
    std::vector<std::string> words;
    std::string              prefix;
    std::string              expected;
};

}

TEST_CASE("every inserted bash candidate parses back to its own text", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const auto        delimiter = nucleus::cli_delimiter::parse("$(touch PWN_DELIM)");
    const std::string logging   = script(logging_registry());
    const std::string values    = script(values_registry());
    REQUIRE(delimiter);
    const std::string delimited = script(delimiter_registry(), *delimiter);
    const std::string d         = "$(touch PWN_DELIM)";
    const std::string node      = "--cluster" + d + "node";

    std::vector<insertion_row> rows{
            {logging, {"--logging-level", "=", "h"}, "--logging-level=", "<--logging-level=has space>\n"},
            {values, {"--cluster-node-"}, "", "<--cluster-node-level>\n<--cluster-node-*-level>\n"},
            {delimited, {"--cluster\\$"}, "", "<" + node + ">\n<" + node + d + "port>\n<" + node + d + "*" + d + "port>\n<--cluster" + d + "port>\n"},
            {delimited, {"--cluster\\$\\(touch\\ PWN_DELIM\\)node\\$\\(touch\\ PWN_DELIM\\)3"}, "", "<" + node + d + "3" + d + "port>\n"},
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
            rows.push_back({values, {flag, "=", prefix}, flag + "=", "<" + flag + "=" + value + ">\n"});
    }
    for(const insertion_row &row : rows)
    {
        DYNAMIC_SECTION(row.prefix << row.words.back())
        {
            REQUIRE(run_seeded(row.script, insert_and_parse(row.words, row.prefix), {"--cluster-node-7-level"}) == row.expected);
        }
    }
}

TEST_CASE("a value typed past a colon completes to its own text where bash keeps it one word", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const std::string          values = script(values_registry());
    const std::string          flag   = "--cluster-node-level";
    std::vector<insertion_row> rows{
            {values, {flag + "=co:"}, flag + "=co:", "<" + flag + "=co:lon>\n"},
            {values, {flag + "=co:l"}, flag + "=co:", "<" + flag + "=co:lon>\n"},
            {values, {"--cluster-node-2-level=co:"}, "--cluster-node-2-level=co:", "<--cluster-node-2-level=co:lon>\n"},
            {values, {flag, "=", "'co:"}, flag + "='", "<" + flag + "=co:lon>\n"},
    };
    for(const insertion_row &row : rows)
    {
        DYNAMIC_SECTION(row.words.back())
        {
            REQUIRE(run_seeded(row.script, insert_and_parse(row.words, row.prefix), {}) == row.expected);
        }
    }
    const std::string untrimmed = "COMP_WORDBREAKS=${COMP_WORDBREAKS//:/}\n" + insert_and_parse({flag + "=co:"}, flag + "=");
    REQUIRE(run_seeded(values, untrimmed, {}) == "<" + flag + "=co:lon>\n");
    // bash 4 and later split the value at the colon and nothing re-joins it, so no value is offered.
    REQUIRE(run_seeded(values, insert_and_parse({flag, "=", "co", ":"}, flag + "=co:"), {}).empty());
}

TEST_CASE("every byte but a newline round-trips through the inserted line", "[completion][smoke]")
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
    const std::string drive      = insert_and_parse({"--v", "="}, "--v=");
    REQUIRE(run_seeded(completion, drive, {}) == expected);
    REQUIRE(run_seeded("export LC_ALL=C\n" + completion, drive, {}) == expected);
}
