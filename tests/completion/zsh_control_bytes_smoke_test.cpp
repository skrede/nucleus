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
using nucleus::zsh_test::zsh_session;

namespace {

struct rendered_control
{
    std::string name;
    std::string description;
    std::string drawn;
    std::string raw;
};

const std::string caret_description = std::string("a\x01") + "b\x7f" + "c\x1b" + "[31mred\x08" + "d";

const std::vector<rendered_control> &rendered_controls()
{
    static const std::vector<rendered_control> controls{
            {"b0", "a\xe2\x80\xaaz", "a\\xe2\\x80\\xaaz", "\xe2\x80\xaa"},
            {"b1", "a\xe2\x80\xabz", "a\\xe2\\x80\\xabz", "\xe2\x80\xab"},
            {"b2", "a\xe2\x80\xacz", "a\\xe2\\x80\\xacz", "\xe2\x80\xac"},
            {"b3", "a\xe2\x80\xadz", "a\\xe2\\x80\\xadz", "\xe2\x80\xad"},
            {"b4", "a\xe2\x80\xaez", "a\\xe2\\x80\\xaez", "\xe2\x80\xae"},
            {"b5", "a\xe2\x81\xa6z", "a\\xe2\\x81\\xa6z", "\xe2\x81\xa6"},
            {"b6", "a\xe2\x81\xa7z", "a\\xe2\\x81\\xa7z", "\xe2\x81\xa7"},
            {"b7", "a\xe2\x81\xa8z", "a\\xe2\\x81\\xa8z", "\xe2\x81\xa8"},
            {"b8", "a\xe2\x81\xa9z", "a\\xe2\\x81\\xa9z", "\xe2\x81\xa9"},
            {"c1", std::string("a\xc2\x9b") + "31mz", "a\\xc2\\x9b31mz", "\xc2\x9b"},
            {"bare", std::string("a\x9b") + "31mz", "a\\x9b31mz", std::string("\x9b") + "31m"},
    };
    return controls;
}

const std::vector<std::string> prose{"caf\xc3\xa9", "a\xe2\x9b\x80z", "a\xf0\x9f\x98\x80z", "a\xc2\xa0z"};

void add_described(nucleus::config_space_builder &engine, const char *parent, const std::string &name, const std::string &text)
{
    REQUIRE(engine.register_element(nucleus::described(nucleus::element(name, under(parent)), text)));
}

std::string caret_script()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("c", nucleus::anchor::root())));
    add_described(engine, "c", "a", caret_description);
    add_described(engine, "c", "b", "plain");
    return script_of(built(engine).generate_completion(nucleus::shell::zsh, "myapp"));
}

std::string bidi_script()
{
    const std::vector<std::string> names{"acute", "symbol", "emoji", "nbsp"};
    nucleus::config_space_builder  engine;
    REQUIRE(engine.register_element(nucleus::element("b", nucleus::anchor::root())));
    for(const rendered_control &control : rendered_controls())
        add_described(engine, "b", control.name, control.description);
    for(std::size_t k = 0; k < prose.size(); ++k)
        add_described(engine, "b", names[k], prose[k]);
    return script_of(built(engine).generate_completion(nucleus::shell::zsh, "myapp"));
}

std::string replaced(std::string text, const std::string &from, const std::string &to)
{
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

bool draws(const zsh_session &session, std::string_view text) { return session.cases[0].terminal.find(text) != std::string::npos; }

zsh_session drive(const std::string &script, const std::string &typed) { return drive_zsh({{"_nucleus_myapp", script}}, {typed}); }
}

TEST_CASE("a control byte in a description is drawn as caret text and never sent to the terminal", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive(caret_script(), "myapp --c-");
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    REQUIRE(draws(session, "a^Ab^?c^[[31mred^Hd"));
    REQUIRE_FALSE(draws(session, "\x1b[31m"));
    require_quiet(session);
}

TEST_CASE("a control byte left raw in the spec reaches the terminal", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive(replaced(caret_script(), "a^Ab^?c^\\[\\[31mred^Hd", caret_description), "myapp --c-");
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    REQUIRE(draws(session, "\x1b[31m"));
}

TEST_CASE("a bidi or C1 control in a description is drawn as escaped text, and other non-ASCII prose as itself", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const zsh_session session = drive(bidi_script(), "myapp --b-");
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    for(const rendered_control &control : rendered_controls())
    {
        INFO(control.name);
        CHECK(draws(session, control.drawn));
        CHECK_FALSE(draws(session, control.raw));
    }
    for(const std::string &text : prose)
        CHECK(draws(session, text));
    require_quiet(session);
}

TEST_CASE("a bidi override left raw in the spec reaches the terminal", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string raw     = std::string("[a\xe2\x80\xae") + "z]";
    const zsh_session session = drive(replaced(bidi_script(), "[a\\\\xe2\\\\x80\\\\xaez]", raw), "myapp --b-");
    INFO("zsh " << session.version);
    INFO(session.cases[0].terminal);
    REQUIRE(draws(session, "\xe2\x80\xae"));
}
