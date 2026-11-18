#include "completion/zsh_test_support.h"

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
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_drive_options;
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

// Menu completion steps from `--cl` to `--cluster`, then past it; zsh 5.0.8 at times lets a step insert nothing, hence
// the loop. Running the line through eval inside the widget leaves the driver's prompt synchronization untouched.
const std::string menu_insert_and_run = "myapp() { : }\nnode() { : }\nnucleus_probe()\n{\n    local step\n    for step in 1 2 3 4; do\n"
                                        "        zle menu-complete\n        [[ $BUFFER == 'myapp --cluster'?* ]] && break\n    done\n"
                                        "    nucleus_record \"BUF<$BUFFER>\"\n    nucleus_record \"BEFORE<$(print -r -- PWN_*(N))>\"\n"
                                        "    eval \"$BUFFER\"\n    BUFFER=\n    zle -R\n}\n";

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

std::string script(const schema_registry &reg, std::string_view delimiter = "-")
{
    return script_of(generate_completion(shell::zsh, reg, "myapp", cli_delimiter::parse(delimiter).value()));
}

zsh_session drive(const std::string &script, const std::string &typed, const zsh_drive_options &options = {}) { return drive_zsh({{"_nucleus_myapp", script}}, {typed}, options); }

std::vector<std::string> buffer_of(const zsh_session &session) { return records_of(session.cases[0], "BUF"); }

bool offers_unquoted(const zsh_session &session, std::string_view delimiter)
{
    const std::vector<std::string> hits = records_of(session.cases[0], "HIT");
    return std::ranges::any_of(hits, [delimiter](const std::string &hit)
                               { return hit.find(delimiter) != std::string::npos; });
}

void require_ran_cleanly(const zsh_session &session)
{
    const std::string terminal = session.setup + session.cases[0].terminal;
    INFO(terminal);
    for(const std::string_view text : {"_arguments:", "comparguments", "command not found"})
        REQUIRE(terminal.find(text) == std::string::npos);
}
}

TEST_CASE("a typed backslash-escaped star under a star delimiter completes the flag", "[completion][smoke][zsh][ordinal]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string starred = "myapp --cluster\\*node\\*2\\*";
    const zsh_session session = drive(script(port_registry(), "*"), starred);
    INFO("zsh " << session.version);
    CHECK(buffer_of(session) == std::vector<std::string>{below_5_9(session.version) ? starred : starred + "port "});
    require_quiet(session);
    const std::string delimiter = "\\$\\(touch\\ PWN_DELIM\\)";
    const std::string typed     = "myapp --cluster" + delimiter + "node" + delimiter + "2" + delimiter;
    const zsh_session hostile   = drive(script(delim_registry(), "$(touch PWN_DELIM)"), typed);
    REQUIRE(hostile.payload.empty());
    CHECK(buffer_of(hostile) == std::vector<std::string>{below_5_9(hostile.version) ? typed : typed + "port "});
    require_quiet(hostile);
}

TEST_CASE("below zsh 5.9 a flag carrying a character zsh quotes is inserted unquoted or not at all", "[completion][smoke][zsh][ordinal]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string spaced_typed = "myapp --cluster\\ node\\ 2\\ ";
    const zsh_session starred      = drive(script(port_registry(), "*"), "myapp --cluster*node*2*");
    const zsh_session wildcard     = drive(script(port_registry()), "myapp --cluster-node-\\*-p");
    const zsh_session spaced       = drive(script(port_registry(), " "), spaced_typed);
    const bool        old          = below_5_9(starred.version);
    INFO("zsh " << starred.version);
    CHECK(buffer_of(starred) == std::vector<std::string>{old ? "myapp --cluster*node*2*port " : "myapp --cluster\\*node\\*2\\*port "});
    CHECK(buffer_of(wildcard) == std::vector<std::string>{old ? "myapp --cluster-node-\\*-p" : "myapp --cluster-node-\\*-port "});
    CHECK(buffer_of(spaced) == std::vector<std::string>{old ? spaced_typed : spaced_typed + "port "});
    for(const zsh_session &session : {starred, wildcard, spaced})
        require_quiet(session);
}

TEST_CASE("what zsh inserts for a flag whose delimiter carries shell syntax and what running that line does", "[completion][smoke][zsh][ordinal]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const shell_syntax_delimiter &delimiter : shell_syntax_delimiters)
    {
        const std::string text  = script(delim_registry(), delimiter.text);
        const zsh_session probe = drive(text, "myapp --cl");
        const zsh_session run   = drive(text, "myapp --cl", {.setup = menu_insert_and_run});
        const bool        old   = below_5_9(probe.version);
        INFO("zsh " << probe.version << ", delimiter " << delimiter.text);
        CHECK(buffer_of(probe) == std::vector<std::string>{"myapp --cluster"});
        CHECK(offers_unquoted(probe, delimiter.text) == old);
        require_quiet(probe);
        CHECK(buffer_of(run) == std::vector<std::string>{"myapp --cluster" + std::string(old ? delimiter.text : delimiter.quoted) + "node"});
        CHECK(records_of(run.cases[0], "BEFORE") == std::vector<std::string>{""});
        if(old)
        {
            REQUIRE(run.payload == std::string(delimiter.marker) + "\n");
            require_ran_cleanly(run);
        }
        else
            require_quiet(run);
    }
}
