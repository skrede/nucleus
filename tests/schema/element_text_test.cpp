#include "support/escaped_echo_test_support.h"
#include "support/builder_result_test_support.h"
#include "support/completion_result_test_support.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/utility/escaped_text.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>
#include <algorithm>

namespace {

using nucleus::escaped_echo_test::byte_of;
using nucleus::escaped_echo_test::framed;
using nucleus::escaped_echo_test::printable_ascii;
using nucleus::escaped_echo_test::registration_refusal;
using nucleus::escaped_echo_test::require_escaped_echo;
using nucleus::escaped_echo_test::terminal_controls;
using nucleus::escaped_echo_test::under;

bool holds_c1_range_byte(const std::string &text)
{
    return std::ranges::any_of(text, [](char c)
                               { return byte_of(c) >= 0x80 && byte_of(c) <= 0x9f; });
}

nucleus::schema_element repeated(nucleus::schema_element el)
{
    el.repeated = true;
    return el;
}

std::string refused_message(std::vector<nucleus::schema_element> prior, nucleus::schema_element refused,
                            nucleus::errc code = nucleus::errc::rejected_registration)
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    for(nucleus::schema_element &el : prior)
        REQUIRE(engine.register_element(std::move(el)));
    return registration_refusal(engine.register_element(std::move(refused)), code);
}

std::string attach_refusal(nucleus::schema_element refused)
{
    nucleus::schema_registry registry;
    auto                     attached = registry.attach(std::move(refused));
    REQUIRE_FALSE(attached);
    return attached.error();
}

std::string without_name(std::string script, const std::string &name)
{
    for(auto at = script.find(name); at != std::string::npos; at = script.find(name))
        script.erase(at, name.size());
    return script;
}

}

TEST_CASE("an element name carrying a C1 or bidirectional control is refused at registration",
          "[schema][registration]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));

    const std::vector<std::string> controlled{framed("\xe2\x80\xae"), framed("\xc2\x9b"), framed("\x9b"),
                                              framed("\xe2\x80\x8f"), framed("\xd8\x9c")};
    for(const std::string &name : controlled)
    {
        auto refused = engine.register_element(nucleus::element(name, under("logging")));
        REQUIRE_FALSE(refused);
        REQUIRE(refused.error().code == nucleus::errc::rejected_registration);
        const std::string &message = refused.error().message;
        REQUIRE(message.find("malformed name") != std::string::npos);
        REQUIRE(message.find(nucleus::escaped_text(name)) != std::string::npos);
        REQUIRE(printable_ascii(message));
    }

    auto rtl_override = engine.register_element(nucleus::element(controlled.front(), under("logging")));
    REQUIRE_FALSE(rtl_override);
    REQUIRE(rtl_override.error().message.find("x\\xe2\\x80\\xaey") != std::string::npos);
}

TEST_CASE("an element name of non-ASCII prose still registers", "[schema][registration]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    for(const char *admitted : {"café", "größe", "log_level.2"})
        REQUIRE(engine.register_element(nucleus::element(admitted, under("logging"))));
}

TEST_CASE("the completion scripts of admitted prose names carry no C1-range byte of their own",
          "[schema][registration][completion]")
{
    const std::vector<std::string> names{"café", "größe"};
    nucleus::config_space_builder  engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    for(const std::string &name : names)
        REQUIRE(engine.register_element(nucleus::element(name, under("logging"))));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    for(const nucleus::shell target : {nucleus::shell::bash, nucleus::shell::zsh})
    {
        const auto script = space.generate_completion(target, "mytool");
        REQUIRE(script);
        std::string text = script.value();
        std::erase(text, '\\');
        for(const std::string &name : names)
        {
            REQUIRE(text.find(name) != std::string::npos);
            text = without_name(std::move(text), name);
        }
        REQUIRE_FALSE(holds_c1_range_byte(text));
    }
}

TEST_CASE("an allowed value carrying a display control is refused at registration",
          "[schema][registration]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));

    const std::vector<std::string> controlled{"a\nb", std::string("a\0b", 3), "\x1b[2J", "a\tb", framed("\x7f"),
                                              framed("\xe2\x80\xae"), framed("\xc2\x9b")};
    for(const std::string &value : controlled)
    {
        auto refused = engine.register_element(nucleus::enum_element("level", under("logging"), {"ok", value}));
        REQUIRE_FALSE(refused);
        REQUIRE(refused.error().code == nucleus::errc::rejected_registration);
        const std::string &message = refused.error().message;
        REQUIRE(message.find("'level'") != std::string::npos);
        REQUIRE(message.find("allowed value") != std::string::npos);
        REQUIRE(message.find(nucleus::escaped_text(value)) != std::string::npos);
        REQUIRE(printable_ascii(message));
    }
}

TEST_CASE("an allowed value of spaces, shell metacharacters or prose still registers",
          "[schema][registration]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
            nucleus::enum_element("level", under("logging"), {"has space", "x;y", "*", "größe", "~root"})));
}

TEST_CASE("a registration refusal echoes the control-carrying text it refuses escaped", "[schema][registration]")
{
    for(const std::string &control : terminal_controls)
    {
        const std::string             name        = framed(control);
        const std::string             digit_led   = "1" + control + "x";
        const std::string             bad_anchor  = "a//" + control;
        const std::string             undefined   = "nowhere" + control;
        const nucleus::anchor         logging     = under("logging");
        const nucleus::schema_element misanchored = nucleus::element("x", nucleus::anchor::keyspace(bad_anchor));
        require_escaped_echo(refused_message({}, nucleus::element(digit_led, logging)), "digit-led", digit_led);
        require_escaped_echo(attach_refusal(nucleus::element(name, nucleus::anchor::keyspace(std::string("a//b")))), "malformed keyspace anchor", name);
        require_escaped_echo(attach_refusal(misanchored), "malformed keyspace anchor", bad_anchor);
        require_escaped_echo(refused_message({}, misanchored, nucleus::errc::malformed_source), "is malformed", bad_anchor);
        require_escaped_echo(refused_message({}, nucleus::element(name, under("nowhere"))), "undefined keyspace", name);
        require_escaped_echo(refused_message({}, nucleus::element("x", nucleus::anchor::keyspace(undefined))), "undefined keyspace", undefined);
        require_escaped_echo(refused_message({nucleus::identity_element("id", logging)}, nucleus::identity_element(name, logging)),
                             "already the config space's primary key", name);
        require_escaped_echo(refused_message({}, repeated(nucleus::identity_element(name, logging))), "both repeated and a primary key", name);
        require_escaped_echo(refused_message({}, repeated(nucleus::unique_element(name, logging))), "both repeated and unique", name);
        require_escaped_echo(refused_message({}, nucleus::keyref_element("ref", logging, "g" + control)), "references identity namespace", "g" + control);
    }
}

TEST_CASE("a schema path is refused unless each of its segments is a well-formed element name", "[schema][registration][completion]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_schema("logging/level"));
    std::vector<std::string> refused{"", "a//b", "v1/2fa", "my flag", "a;b", "node[0]"};
    for(const std::string &control : terminal_controls)
        refused.insert(refused.end(), {"x" + control, "logging/x" + control + "y"});
    for(const std::string &path : refused)
        require_escaped_echo(registration_refusal(engine.register_schema(path)), "schema path", path);
    REQUIRE(engine.schema_count() == 1);
    for(const std::string &control : terminal_controls)
        require_escaped_echo(registration_refusal(engine.register_element(nucleus::element("y", nucleus::anchor::keyspace("x" + control)))), "undefined keyspace", "x" + control);
    REQUIRE(engine.register_schema("log_level.2/a-b"));
    const nucleus::config_space space = nucleus::builder_result_test::built(engine);
    for(const std::string &text : {nucleus::completion_result_test::script_of(space.generate_completion(nucleus::shell::bash, "mytool")), nucleus::completion_result_test::script_of(space.generate_completion(nucleus::shell::zsh, "mytool")), space.generate_help("mytool")})
    {
        REQUIRE(text.find("--logging-level") != std::string::npos);
        REQUIRE(text.find("--log_level.2-a-b") != std::string::npos);
        REQUIRE(text.find_first_of("\x1b\xe2\x80\xae") == std::string::npos);
    }
}
