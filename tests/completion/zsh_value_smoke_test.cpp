#include "completion/zsh_test_support.h"
#include "completion/admitted_values_test_support.h"

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

using nucleus::admitted_values_test::admitted_bytes;
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

const std::vector<std::string> decoys{"decoy1", "decoyX", "a", "b", "zzz"};

std::string script_with(const char *name, const std::vector<std::string> &values)
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("app", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::enum_element(name, under("app"), values)));
    return script_of(built(engine).generate_completion(nucleus::shell::zsh, "myapp"));
}

std::string replaced(std::string text, const std::string &from, const std::string &to)
{
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

std::vector<std::string> sorted(std::vector<std::string> values)
{
    std::ranges::sort(values);
    return values;
}

bool names_decoy(const zsh_seen &seen)
{
    const auto decoy = [](const std::string &match)
    { return std::ranges::find(decoys, match) != decoys.end(); };
    return std::ranges::any_of(records_of(seen, "HIT"), decoy) || std::ranges::any_of(records_of(seen, "VALUE"), decoy);
}

zsh_session drive_globs(const std::string &script, const std::vector<std::string> &typed)
{
    return drive_zsh({{"_nucleus_myapp", script}}, typed, {.decoys = decoys});
}

}

TEST_CASE("a glob in an allowed value is offered literally under real zsh beside files it would match", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive_globs(script_with("glob", {"*", "decoy*", "[a-z]*"}), {"myapp --app-glob=", "myapp --app-glob=dec"});
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    REQUIRE(sorted(records_of(session.cases[0], "VALUE")) == std::vector<std::string>{"*", "[a-z]*", "decoy*"});
    REQUIRE_FALSE(names_decoy(session.cases[0]));
    REQUIRE_FALSE(names_decoy(session.cases[1]));
    REQUIRE(records_of(session.cases[1], "BUF") == std::vector<std::string>{"myapp --app-glob=decoy\\* "});
    require_quiet(session);
}

TEST_CASE("a value group stripped of its escapes globs the working directory", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string script  = replaced(script_with("glob", {"*", "decoy*", "[a-z]*"}), "(\\* decoy\\* \\[a-z\\]\\*)", "(* decoy* [a-z]*)");
    const zsh_session session = drive_globs(script, {"myapp --app-glob="});
    INFO("zsh " << session.version);
    INFO(session.cases[0].records);
    REQUIRE(names_decoy(session.cases[0]));
}

TEST_CASE("shell syntax in an allowed value is offered as text and inserted escaped", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::vector<std::string> values{"$(touch PWN_V)", "`touch PWN_BQ`", "x;touch PWN_SEMI", "has space", "co:lon", "back\\slash"};
    const zsh_session              session = drive_zsh({{"_nucleus_myapp", script_with("pick", values)}}, {"myapp --app-pick=", "myapp --app-pick=\\$", "myapp --app-pick=\\`"});
    INFO("zsh " << session.version);
    INFO(session.cases[0].records);
    REQUIRE(sorted(records_of(session.cases[0], "VALUE")) == sorted(values));
    CHECK(records_of(session.cases[1], "BUF") == std::vector<std::string>{"myapp --app-pick=\\$\\(touch\\ PWN_V\\) "});
    CHECK(records_of(session.cases[2], "BUF") == std::vector<std::string>{"myapp --app-pick=\\`touch\\ PWN_BQ\\` "});
    REQUIRE(session.payload.empty());
    require_quiet(session);
}

TEST_CASE("every allowed value the registration rule admits round-trips through the generated zsh script", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::vector<std::string> values  = admitted_bytes();
    const zsh_session              session = drive_zsh({{"_nucleus_myapp", script_with("mode", values)}}, {"myapp --app-mode="});
    INFO("zsh " << session.version);
    REQUIRE(values.size() == 191);
    REQUIRE(sorted(records_of(session.cases[0], "VALUE")) == sorted(values));
    REQUIRE(session.payload.empty());
}
