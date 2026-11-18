#include "completion/zsh_test_support.h"

#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

using nucleus::anchor;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::completion_result_test::script_of;
using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::records_of;
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::route_text;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_route;
using nucleus::zsh_test::zsh_session;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

std::string logging_script()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("logging", anchor::root())));
    REQUIRE(reg.attach(nucleus::described(nucleus::enum_element("level", anchor::keyspace(path_of("logging")), {"debug", "info", "warn", "error"}), "set the level")));
    return script_of(generate_completion(shell::zsh, reg, "myapp"));
}

constexpr std::array<zsh_route, 4>        source_routes{zsh_route::source, zsh_route::source_bare, zsh_route::source_dot, zsh_route::source_path};
constexpr std::array<std::string_view, 3> source_files{"_myapp", "myapp.zsh", "_myapp.zsh"};
}

TEST_CASE("the generated zsh script installed on fpath completes under a real zsh", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive_zsh({{"_myapp", logging_script()}}, {"myapp --log", "myapp --logging-level=", "myapp --logging-level=w"});
    REQUIRE(session.registered == "_myapp");
    REQUIRE(records_of(session.cases[0], "HIT") == std::vector<std::string>{"--logging", "--logging-level"});
    REQUIRE(records_of(session.cases[1], "VALUE") == std::vector<std::string>{"debug", "info", "warn", "error"});
    REQUIRE(records_of(session.cases[2], "BUF") == std::vector<std::string>{"myapp --logging-level=warn "});
    require_quiet(session);
}

TEST_CASE("sourcing the generated zsh script after compinit registers its completion and prints nothing", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive_zsh({{"myapp.zsh", logging_script()}}, {"myapp --logging-level=w"}, {.route = zsh_route::source});
    INFO(session.setup);
    REQUIRE(session.registered == "_myapp");
    REQUIRE(records_of(session.cases[0], "BUF") == std::vector<std::string>{"myapp --logging-level=warn "});
    require_quiet(session);
}

TEST_CASE("the source route fails visibly when the script ends in a bare call", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string script = logging_script();
    const std::string bare   = script.substr(0, script.rfind('\n', script.size() - 2) + 1) + "_myapp \"$@\"\n";
    REQUIRE(bare != script);
    const zsh_session session = drive_zsh({{"myapp.zsh", bare}}, {"myapp --logging-level=w"}, {.route = zsh_route::source});
    REQUIRE(session.setup.find("can only be called from completion function") != std::string::npos);
    REQUIRE(session.registered == "unset");
}

TEST_CASE("sourcing the generated zsh script after compinit registers it whatever the file is named and however source finds it", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const zsh_route route : source_routes)
        for(const std::string_view file : source_files)
            DYNAMIC_SECTION(route_text(route) << " " << file)
            {
                const zsh_session session = drive_zsh({{std::string(file), logging_script()}}, {"myapp --logging-level=w"}, {.route = route});
                INFO(session.setup);
                REQUIRE(session.registered == "_myapp");
                REQUIRE(records_of(session.cases[0], "BUF") == std::vector<std::string>{"myapp --logging-level=warn "});
                require_quiet(session);
            }
}

TEST_CASE("evaluating the generated zsh script after compinit registers its completion and prints nothing", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive_zsh({{"myapp.zsh", logging_script()}}, {"myapp --logging-level=w"}, {.route = zsh_route::eval});
    INFO(session.setup);
    REQUIRE(session.registered == "_myapp");
    REQUIRE(records_of(session.cases[0], "BUF") == std::vector<std::string>{"myapp --logging-level=warn "});
    require_quiet(session);
}

TEST_CASE("the eval route fails visibly when the last line tests only the evaluation context", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    constexpr std::string_view joiner = R"(" ] && )";
    const std::string          script = logging_script();
    const std::size_t          from   = script.find(R"([ "$funcstack[1]" = ")");
    const std::size_t          join   = script.find(joiner, from);
    REQUIRE(from != std::string::npos);
    REQUIRE(join != std::string::npos);
    std::string context_only = script;
    context_only.erase(from, join + joiner.size() - from);
    REQUIRE(context_only != script);
    const zsh_session session = drive_zsh({{"myapp.zsh", context_only}}, {"myapp --logging-level=w"}, {.route = zsh_route::eval});
    INFO(session.setup);
    REQUIRE(session.setup.find("can only be called from completion function") != std::string::npos);
    REQUIRE(session.registered == "unset");
}
