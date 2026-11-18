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
#include <algorithm>
#include <string_view>

using nucleus::builder_result_test::built;
using nucleus::completion_result_test::script_of;
using nucleus::escaped_echo_test::under;
using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::records_of;
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_seen;
using nucleus::zsh_test::zsh_session;

namespace {

void add_described(nucleus::config_space_builder &engine, const char *parent, const char *name, const char *text)
{
    REQUIRE(engine.register_element(nucleus::described(nucleus::element(name, under(parent)), text)));
}

std::string described_script()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("d", nucleus::anchor::root())));
    add_described(engine, "d", "o0", "d]:*:->state");
    add_described(engine, "d", "o1", "d]:m:{ touch PWN_DESC }");
    add_described(engine, "d", "o2", "tail\\");
    add_described(engine, "d", "ok", "plain");
    add_described(engine, "d", "lvl", "set the level (default: info)");
    add_described(engine, "d", "lvx", "other");
    return script_of(built(engine).generate_completion(nucleus::shell::zsh, "myapp"));
}

std::string value_group_script()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("app", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::described(nucleus::enum_element("q8", under("app"), {"x", "y\\z", "w:v"}), "back\\slash [br] key: val")));
    add_described(engine, "app", "q9", "other");
    return script_of(built(engine).generate_completion(nucleus::shell::zsh, "myapp"));
}

std::string replaced(std::string text, const std::string &from, const std::string &to)
{
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

bool draws(const zsh_seen &seen, std::string_view text) { return seen.terminal.find(text) != std::string::npos; }

std::vector<std::string> sorted(std::vector<std::string> values)
{
    std::ranges::sort(values);
    return values;
}

zsh_session drive(const std::string &script, const std::vector<std::string> &typed) { return drive_zsh({{"_nucleus_myapp", script}}, typed); }
}

TEST_CASE("a description cannot reach the spec's action field under real zsh", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive(described_script(), {"myapp --d-o", "myapp --d-ok", "myapp --d-o1 "});
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    REQUIRE(sorted(records_of(session.cases[0], "HIT")) == std::vector<std::string>{"--d-o0", "--d-o1", "--d-o2", "--d-ok"});
    CHECK(draws(session.cases[0], "d]:*:->state"));
    CHECK(draws(session.cases[0], "d]:m:{ touch PWN_DESC }"));
    CHECK(draws(session.cases[0], "tail\\"));
    REQUIRE(records_of(session.cases[1], "BUF") == std::vector<std::string>{"myapp --d-ok "});
    REQUIRE(session.payload.empty());
    require_quiet(session);
}

TEST_CASE("a description whose closing bracket is left unescaped hands the rest to the action field", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string script  = replaced(described_script(), "'--d-o1[d\\]\\:m\\:{ touch PWN_DESC }]'", "'--d-o1[d]:m:{ touch PWN_DESC }'");
    const zsh_session session = drive(script, {"myapp --d-o1 "});
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    REQUIRE(session.payload == "PWN_DESC\n");
}

TEST_CASE("a description ending in an unescaped backslash leaves its program with no completion", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive(replaced(described_script(), "[tail\\\\]", "[tail\\]"), {"myapp --d-ok"});
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    REQUIRE(draws(session.cases[0], "invalid option definition"));
    REQUIRE(records_of(session.cases[0], "HIT").empty());
    REQUIRE(records_of(session.cases[0], "BUF") == std::vector<std::string>{"myapp --d-ok"});
}

TEST_CASE("a colon in a description is drawn without a backslash, escaped or not", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string script  = described_script();
    const zsh_session escaped = drive(script, {"myapp --d-lv"});
    const zsh_session raw     = drive(replaced(script, "(default\\: info)", "(default: info)"), {"myapp --d-lv"});
    INFO("zsh " << escaped.version);
    INFO(escaped.cases[0].terminal);
    REQUIRE(draws(escaped.cases[0], "set the level (default: info)"));
    REQUIRE_FALSE(draws(escaped.cases[0], "default\\: info"));
    REQUIRE(raw.cases[0].terminal == escaped.cases[0].terminal);
    require_quiet(escaped);
    require_quiet(raw);
}

TEST_CASE("description escapes survive beside a value group", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive(value_group_script(), {"myapp --app-q", "myapp --app-q8="});
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    REQUIRE(draws(session.cases[0], "back\\slash [br] key: val"));
    REQUIRE(sorted(records_of(session.cases[1], "VALUE")) == std::vector<std::string>{"w:v", "x", "y\\z"});
    require_quiet(session);
}
