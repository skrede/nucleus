#include "completion/zsh_test_support.h"

#include "support/escaped_echo_test_support.h"
#include "support/builder_result_test_support.h"
#include "support/completion_result_test_support.h"

#include "nucleus/config_space.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

using nucleus::builder_result_test::built;
using nucleus::completion_result_test::script_of;
using nucleus::escaped_echo_test::under;
using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_drive_options;
using nucleus::zsh_test::zsh_session;

namespace {

const std::string percent = "100% %F{red}red%f %B bold %b %{x%} %n %~";

const std::string described_groups = "zstyle ':completion:*:descriptions' format '%B-- %d --%b'\nzstyle ':completion:*' group-name ''\n";

// complist enters menu selection only on a terminal whose capabilities zsh knows, so this session names one.
const std::string menu_selection = "TERM=xterm\nzmodload zsh/complist\nzstyle ':completion:*' menu select\n";

const std::vector<std::string> shell_syntax{"d$(touch PWN_DS)x", "d`touch PWN_BQ`x", "d;touch PWN_SEMI", "d|id&"};

void add_described(nucleus::config_space_builder &engine, const char *parent, const char *name, const std::string &text)
{
    REQUIRE(engine.register_element(nucleus::described(nucleus::element(name, under(parent)), text)));
}

std::string percent_script()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("p", nucleus::anchor::root())));
    add_described(engine, "p", "pct", percent);
    add_described(engine, "p", "rest", "plain");
    return script_of(built(engine).generate_completion(nucleus::shell::zsh, "myapp"));
}

std::string shell_syntax_script()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("s", nucleus::anchor::root())));
    add_described(engine, "s", "ds", shell_syntax[0]);
    add_described(engine, "s", "bq", shell_syntax[1]);
    add_described(engine, "s", "semi", shell_syntax[2]);
    add_described(engine, "s", "pipe", shell_syntax[3]);
    return script_of(built(engine).generate_completion(nucleus::shell::zsh, "myapp"));
}

zsh_session drive(const std::string &script, const std::string &typed, const zsh_drive_options &options = {})
{
    return drive_zsh({{"_myapp", script}}, {typed}, options);
}

bool draws(const zsh_session &session, std::string_view text) { return session.cases[0].terminal.find(text) != std::string::npos; }

}

TEST_CASE("a percent sign in a description is drawn literally in every listing", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string script   = percent_script();
    const zsh_session plain    = drive(script, "myapp --p-");
    const zsh_session headed   = drive(script, "myapp --p-", {.setup = described_groups});
    const zsh_session selected = drive(script, "myapp --p-", {.keys = "\t\t\x07", .setup = menu_selection});
    INFO("zsh " << plain.version);
    INFO(plain.cases[0].terminal);
    INFO(headed.cases[0].terminal);
    INFO(selected.cases[0].terminal);
    CHECK(draws(plain, "-- " + percent));
    CHECK(draws(headed, "-- " + percent));
    CHECK(draws(headed, "-- option --"));
    CHECK_FALSE(draws(plain, "-- option --"));
    const std::size_t highlight = selected.cases[0].terminal.find("\x1b[7m--p-pct");
    REQUIRE(highlight != std::string::npos);
    CHECK(selected.cases[0].terminal.find("-- " + percent, highlight) != std::string::npos);
    require_quiet(plain);
    require_quiet(headed);
    require_quiet(selected);
}

TEST_CASE("shell syntax in a description is drawn verbatim and runs nothing", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive(shell_syntax_script(), "myapp --s-");
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    for(const std::string &text : shell_syntax)
        CHECK(draws(session, " -- " + text));
    REQUIRE(session.payload.empty());
    require_quiet(session);
}
