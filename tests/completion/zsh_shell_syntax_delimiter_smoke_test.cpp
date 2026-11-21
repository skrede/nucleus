#include "completion/zsh_test_support.h"

#include "nucleus/completion/completion.h"
#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <string_view>

using nucleus::anchor;
using nucleus::cli_delimiter;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::completion_result_test::script_of;
using nucleus::zsh_test::below_5_9;
using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::records_of;
using nucleus::zsh_test::require_no_noise;
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::route_text;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_route;
using nucleus::zsh_test::zsh_session;

namespace {

struct shell_syntax_delimiter
{
    std::string_view text;
    std::string_view quoted;
    std::string_view marker;
};

constexpr std::array<shell_syntax_delimiter, 2> shell_syntax_delimiters{{
        {"$(touch PWN_DELIM)", "\\$\\(touch\\ PWN_DELIM\\)", "PWN_DELIM"},
        {";touch PWN_SEMI;", "\\;touch\\ PWN_SEMI\\;", "PWN_SEMI"},
}};

constexpr std::array<zsh_route, 3> install_routes{zsh_route::fpath, zsh_route::source, zsh_route::eval};

// Menu completion steps from `--cl` to `--cluster`, then past it; zsh 5.0.8 at times lets a step insert nothing, hence
// the loop. Running the line through eval inside the widget leaves the driver's prompt synchronization untouched.
const std::string menu_insert_and_run = R"zsh(myapp() { : }
node() { : }
nucleus_probe()
{
    local step
    for step in 1 2 3 4; do
        zle menu-complete
        [[ $BUFFER == 'myapp --cluster'?* ]] && break
    done
    nucleus_record "BUF<$BUFFER>"
    nucleus_record "BEFORE<$(print -r -- PWN_*(N))>"
    eval "$BUFFER"
    BUFFER=
    zle -R
}
)zsh";

key_path path_of(const char *text) { return key_path::parse(text).value(); }

schema_registry port_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node")))));
    return reg;
}

schema_registry delim_registry()
{
    schema_registry reg = port_registry();
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster")))));
    return reg;
}

std::string script(std::string_view delimiter)
{
    return script_of(generate_completion(shell::zsh, delim_registry(), "myapp", cli_delimiter::parse(delimiter).value()));
}

std::string forced_version_test(std::string text)
{
    constexpr std::string_view test = "is-at-least 5.9";
    for(std::size_t at = text.find(test); at != std::string::npos; at = text.find(test, at))
        text.replace(at, test.size(), "true");
    return text;
}

zsh_session drive_on(zsh_route route, const std::string &text, const std::string &setup = "")
{
    const std::string file    = route == zsh_route::fpath ? "_nucleus_myapp" : "myapp.zsh";
    const zsh_session session = drive_zsh({{file, text}}, {"myapp --cl"}, {.route = route, .setup = setup});
    REQUIRE(session.registered == "_nucleus_myapp");
    return session;
}

std::vector<std::string> buffer_of(const zsh_session &session) { return records_of(session.cases[0], "BUF"); }

bool offers(const zsh_session &session, std::string_view delimiter)
{
    const std::vector<std::string> hits = records_of(session.cases[0], "HIT");
    return std::ranges::any_of(hits, [delimiter](const std::string &hit)
                               { return hit.find(delimiter) != std::string::npos; });
}

std::string inserted(const shell_syntax_delimiter &delimiter, bool raw)
{
    return "myapp --cluster" + std::string(raw ? delimiter.text : delimiter.quoted) + "node";
}

void require_run(const zsh_session &run, const std::string &buffer, const std::string &payload)
{
    CHECK(buffer_of(run) == std::vector<std::string>{buffer});
    CHECK(records_of(run.cases[0], "BEFORE") == std::vector<std::string>{""});
    require_no_noise(run);
    REQUIRE(run.payload == payload);
}
}

TEST_CASE("a flag whose delimiter carries shell syntax is offered quoted on zsh 5.9 and not at all before it on every install route", "[completion][smoke][zsh][ordinal]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const zsh_route route : install_routes)
        for(const shell_syntax_delimiter &delimiter : shell_syntax_delimiters)
            DYNAMIC_SECTION(route_text(route) << " " << delimiter.marker)
            {
                const std::string text  = script(delimiter.text);
                const zsh_session probe = drive_on(route, text);
                const zsh_session run   = drive_on(route, text, menu_insert_and_run);
                const bool        old   = below_5_9(probe.version);
                INFO("zsh " << probe.version << ", delimiter " << delimiter.text);
                CHECK(buffer_of(probe) == std::vector<std::string>{old ? "myapp --cluster " : "myapp --cluster"});
                CHECK_FALSE(offers(probe, delimiter.text));
                require_quiet(probe);
                require_run(run, old ? "myapp --cluster " : inserted(delimiter, false), "");
            }
}

TEST_CASE("a script whose version test is forced true inserts a shell-syntax delimiter raw before zsh 5.9 on every install route", "[completion][smoke][zsh][ordinal]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const zsh_route route : install_routes)
        for(const shell_syntax_delimiter &delimiter : shell_syntax_delimiters)
            DYNAMIC_SECTION(route_text(route) << " " << delimiter.marker)
            {
                const std::string text   = script(delimiter.text);
                const std::string forced = forced_version_test(text);
                REQUIRE(forced != text);
                const zsh_session run = drive_on(route, forced, menu_insert_and_run);
                const bool        old = below_5_9(run.version);
                INFO("zsh " << run.version << ", delimiter " << delimiter.text);
                require_run(run, inserted(delimiter, old), old ? std::string(delimiter.marker) + "\n" : "");
            }
}
