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

#include <string>
#include <vector>
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

zsh_session drive_tool(const std::string &file, zsh_route route, const std::vector<std::string> &typed)
{
    return drive_zsh({{file, tool_script()}}, typed, {.route = route, .prog = "my-tool.v2"});
}
}

TEST_CASE("a program name that is not an identifier completes from the file named for its function", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const auto &[file, route] : {std::pair{"_nucleus_my_tool_v2", zsh_route::fpath}, std::pair{"my-tool.v2.zsh", zsh_route::source}})
    {
        const zsh_session session = drive_tool(file, route, {"my-tool.v2 --log"});
        INFO(file);
        REQUIRE(session.registered == "_nucleus_my_tool_v2");
        REQUIRE(sorted_hits(session.cases[0]) == logging_flags);
        require_quiet(session);
    }
    // The first TAB runs the file body, whose last line registers the function it defines; the second completes.
    for(const std::string file : {"_my-tool.v2", "_my_tool_v2"})
    {
        const zsh_session named = drive_tool(file, zsh_route::fpath, {"my-tool.v2 --log", "my-tool.v2 --log"});
        INFO(file);
        REQUIRE(named.registered == file);
        REQUIRE(records_of(named.cases[0], "HIT").empty());
        REQUIRE(records_of(named.cases[0], "BUF") == std::vector<std::string>{"my-tool.v2 --log"});
        REQUIRE(sorted_hits(named.cases[1]) == logging_flags);
        REQUIRE(records_of(named.cases[1], "BUF") == std::vector<std::string>{"my-tool.v2 --logging"});
        require_quiet(named);
    }
}

TEST_CASE("a hostile program name runs nothing when the zsh script is installed either way", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string hostile = "x$(touch PWN_PROG);y\ntouch PWN_LINE";
    const std::string ident   = "x__touch_PWN_PROG__y_touch_PWN_LINE";
    const std::string script  = zsh_emitter{}.emit({hostile, {{"--w", "", {}, {}}}});
    const zsh_session fpath   = drive_zsh({{"_nucleus_" + ident, script}}, {"PWN_LINE --"}, {.prog = "PWN_LINE"});
    REQUIRE(fpath.registered == "_nucleus_" + ident);
    REQUIRE(records_of(fpath.cases[0], "BUF") == std::vector<std::string>{"PWN_LINE --w "});
    require_quiet(fpath);
    const zsh_session sourced = drive_zsh({{"hostile.zsh", script}}, {"PWN_LINE --"}, {.route = zsh_route::source, .prog = "x$(touch PWN_PROG);ytouch PWN_LINE"});
    REQUIRE(sourced.registered == "_nucleus_" + ident);
    require_quiet(sourced);
}
