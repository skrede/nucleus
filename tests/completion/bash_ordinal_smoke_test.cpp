#include "completion/bash_test_support.h"

#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using nucleus::anchor;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::bash_test::bash_available;
using nucleus::bash_test::run_bash;
using nucleus::bash_test::run_seeded;
using nucleus::completion_result_test::script_of;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

schema_registry port_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node")))));
    return reg;
}

schema_registry level_registry()
{
    schema_registry reg = port_registry();
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("cluster/node")),
                                             {"debug", "info"})));
    return reg;
}

schema_registry route_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::repeated_element("route", anchor::keyspace(path_of("cluster/node")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node/route")))));
    return reg;
}

std::string script(const schema_registry &reg, const key_path &at = {})
{
    return script_of(generate_completion(shell::bash, reg, "myapp", {}, at));
}

// Completes the last of `words` the way bash does, one candidate per line and nothing at all
// when COMPREPLY is empty.
std::string drive(const std::string &words)
{
    const std::string out = "\nCOMP_WORDS=(myapp " + words + ")\nCOMP_CWORD=$((${#COMP_WORDS[@]} - 1))\n";
    return out + "_myapp_complete\nfor c in \"${COMPREPLY[@]}\"; do printf '%s\\n' \"$c\"; done\n";
}

struct completion_row
{
    std::string script;
    std::string words;
    std::string expected;
};

}

TEST_CASE("a typed ordinal completes the repeated-container flag under real bash",
          "[completion][smoke][ordinal]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const std::string port     = script(port_registry());
    const std::string level    = script(level_registry());
    const std::string route    = script(route_registry());
    const std::string anchored = script(level_registry(), path_of("cluster/node"));

    const std::vector<completion_row> rows{
            {port, "--cluster-node-2-", "--cluster-node-2-port\n"},
            {port, "--cluster-node-2", "--cluster-node-2-port\n"},
            {port, "--cluster-node-", "--cluster-node-port\n--cluster-node-\\*-port\n"},
            {port, "--cluster-node-2x-", ""},
            {port, "--cluster-node-2-portx", ""},
            {level, "--cluster-node-2-level =", "debug\ninfo\n"},
            {level, "--cluster-node-2-level=", "debug\ninfo\n"},
            {level, "--cluster-node-2-level = d", "debug\n"},
            {level, "--cluster-node-2-level=d", "debug\n"},
            {level, "--cluster-node-level =", "debug\ninfo\n"},
            {level, "--cluster-node-level=", "debug\ninfo\n"},
            {level, "--cluster-node-2-lev =", ""},
            {level, "--cluster-node-x-level =", ""},
            {route, "--cluster-node-0-route-2-", "--cluster-node-0-route-2-port\n"},
            {route, "--cluster-node-0-r", "--cluster-node-0-route\n--cluster-node-0-route-\\*-port\n"},
            {anchored, "--0-", "--0-level\n--0-port\n"},
            {anchored, "--", "--level\n--\\*-level\n--port\n--\\*-port\n"},
    };
    for(const completion_row &row : rows)
    {
        DYNAMIC_SECTION(row.words)
        {
            REQUIRE(run_bash(row.script + drive(row.words)) == row.expected);
        }
    }
}

// The completed word keeps a `*` for the slot the user has not reached, and the directory holds
// names that pattern matches; the second line proves the seeding would have globbed it.
TEST_CASE("an ordinal candidate keeps its wildcard beside files it would match",
          "[completion][smoke][ordinal]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const std::string out = run_seeded(
            script(route_registry()),
            drive("--cluster-node-2-route-") + "set -- --cluster-node-2-route-*-port; echo \"$#\"\n",
            {"--cluster-node-2-route-7-port", "--cluster-node-2-route-9-port"});
    REQUIRE(out == "--cluster-node-2-route-\\*-port\n2\n");
}

TEST_CASE("an ordinal completion leaves the caller's glob setting as it found it",
          "[completion][smoke][ordinal]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const std::string completion = script(level_registry());
    const std::string expected   = "--cluster-node-2-level\n--cluster-node-2-port\ndebug\ninfo\nkept\n";

    std::string probe = "\nbefore=$-\n" + drive("--cluster-node-2-") + drive("--cluster-node-2-level =");
    probe += "if [ \"$-\" = \"$before\" ]; then echo kept; else echo \"$before -> $-\"; fi\n";
    REQUIRE(run_bash("set +f\n" + completion + probe) == expected);
    REQUIRE(run_bash("set -f\n" + completion + probe) == expected);
}
