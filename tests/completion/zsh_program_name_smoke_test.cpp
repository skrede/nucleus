#include "completion/zsh_test_support.h"

#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/completion_model.h"
#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <tuple>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>

using nucleus::anchor;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::zsh_emitter;
using nucleus::completion_result_test::script_of;
using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::records_of;
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::route_text;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_route;
using nucleus::zsh_test::zsh_session;

namespace {

const std::vector<std::string> logging_flags{"--logging", "--logging-level"};

std::string tool_script()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("logging", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(key_path::parse("logging").value()), {"debug", "warn"})));
    return script_of(generate_completion(shell::zsh, reg, "my-tool.v2"));
}

std::vector<std::string> sorted_hits(const nucleus::zsh_test::zsh_seen &seen)
{
    std::vector<std::string> hits = records_of(seen, "HIT");
    std::ranges::sort(hits);
    return hits;
}

std::string alpha_script(const std::string &prog)
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("alpha", anchor::root())));
    return script_of(generate_completion(shell::zsh, reg, prog));
}

// zsh reports the runaway recursion in one of two wordings, depending on its release.
bool recursion_reported(const std::string &terminal)
{
    return terminal.find("maximum nested function level reached") != std::string::npos || terminal.find("recursion limit exceeded") != std::string::npos;
}

zsh_session drive_tool(const std::string &file, zsh_route route, const std::vector<std::string> &typed)
{
    return drive_zsh({{file, tool_script()}}, typed, {.route = route, .prog = "my-tool.v2"});
}

struct compsys_row
{
    std::string name;
    std::string own_buf;
    bool        fallback_offers_alpha;
};

const std::vector<compsys_row> compsys_rows{{"files", "files --alpha ", true}, {"values", "values --alpha ", false}};

std::vector<nucleus::zsh_test::zsh_script> named_scripts(zsh_route route, const std::string &name)
{
    if(route == zsh_route::fpath)
        return {{"_myapp", alpha_script("myapp")}, {"_" + name, alpha_script(name)}};
    return {{"myapp.zsh", alpha_script("myapp")}, {name + ".zsh", alpha_script(name)}};
}

bool offers_alpha(const nucleus::zsh_test::zsh_seen &seen)
{
    const std::vector<std::string> hits = records_of(seen, "HIT");
    return std::ranges::find(hits, "--alpha") != hits.end();
}
}

TEST_CASE("a program name that is not an identifier completes from the file named for its function", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const auto &[file, route] : {std::pair{"_my_tool_v2", zsh_route::fpath}, std::pair{"my-tool.v2.zsh", zsh_route::source}})
    {
        const zsh_session session = drive_tool(file, route, {"my-tool.v2 --log"});
        INFO(file);
        REQUIRE(session.registered == "_my_tool_v2");
        REQUIRE(sorted_hits(session.cases[0]) == logging_flags);
        require_quiet(session);
    }
    // The first TAB runs the file body, whose last line registers the function it defines; the second completes.
    const zsh_session raw = drive_tool("_my-tool.v2", zsh_route::fpath, {"my-tool.v2 --log", "my-tool.v2 --log"});
    REQUIRE(raw.registered == "_my-tool.v2");
    REQUIRE(records_of(raw.cases[0], "HIT").empty());
    REQUIRE(records_of(raw.cases[0], "BUF") == std::vector<std::string>{"my-tool.v2 --log"});
    REQUIRE(sorted_hits(raw.cases[1]) == logging_flags);
    REQUIRE(records_of(raw.cases[1], "BUF") == std::vector<std::string>{"my-tool.v2 --logging"});
    require_quiet(raw);
}

TEST_CASE("a hostile program name runs nothing when the zsh script is installed either way", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string hostile = "x$(touch PWN_PROG);y\ntouch PWN_LINE";
    const std::string ident   = "x__touch_PWN_PROG__y_touch_PWN_LINE";
    const std::string script  = zsh_emitter{}.emit({hostile, {{"--w", "", {}, {}}}});
    const zsh_session fpath   = drive_zsh({{"_" + ident, script}}, {"PWN_LINE --"}, {.prog = "PWN_LINE"});
    REQUIRE(fpath.registered == "_" + ident);
    REQUIRE(records_of(fpath.cases[0], "BUF") == std::vector<std::string>{"PWN_LINE --w "});
    require_quiet(fpath);
    const zsh_session sourced = drive_zsh({{"hostile.zsh", script}}, {"PWN_LINE --"}, {.route = zsh_route::source, .prog = "x$(touch PWN_PROG);ytouch PWN_LINE"});
    REQUIRE(sourced.registered == "_" + ident);
    require_quiet(sourced);
}

// The script for `tags` defines `_tags`, replacing the compsys function `_arguments` itself calls, so every
// completion that goes through `_arguments` recurses until zsh gives up.
TEST_CASE("a program named after a zsh completion function gets no completion", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::vector<std::string> typed{"tags --a", "myapp --a", "ls --al"};
    for(const auto &[route, tags, myapp] : {std::tuple{zsh_route::fpath, "_tags", "_myapp"}, std::tuple{zsh_route::source, "tags.zsh", "myapp.zsh"}})
    {
        const zsh_session session = drive_zsh({{myapp, alpha_script("myapp")}, {tags, alpha_script("tags")}}, typed, {.route = route});
        INFO(route_text(route));
        REQUIRE(session.registered == "_myapp");
        REQUIRE(session.payload.empty());
        for(std::size_t k = 0; k < typed.size(); ++k)
        {
            INFO(session.cases[k].terminal);
            CHECK(records_of(session.cases[k], "HIT").empty());
            CHECK(records_of(session.cases[k], "BUF") == std::vector<std::string>{typed[k]});
            CHECK(recursion_reported(session.cases[k].terminal));
        }
    }
}

// A command with no completion of its own falls back to compsys's `_files`, so it offers what the program
// named `files` defines.
TEST_CASE("a program named after a zsh completion function replaces that function for the whole shell", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const zsh_route route : {zsh_route::fpath, zsh_route::source})
        for(const compsys_row &row : compsys_rows)
        {
            const zsh_session session = drive_zsh(named_scripts(route, row.name), {row.name + " --a", "myapp --a", "othertool --al"}, {.route = route});
            INFO(route_text(route) << " " << row.name);
            REQUIRE(session.registered == "_myapp");
            require_quiet(session);
            CHECK(records_of(session.cases[0], "BUF") == std::vector<std::string>{row.own_buf});
            CHECK(offers_alpha(session.cases[1]));
            CHECK(offers_alpha(session.cases[2]) == row.fallback_offers_alpha);
        }
}
