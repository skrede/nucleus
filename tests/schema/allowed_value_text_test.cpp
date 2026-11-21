#include "support/escaped_echo_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/utility/escaped_text.h"

#include "nucleus/error.h"
#include "nucleus/config_space.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

using nucleus::escaped_echo_test::framed;
using nucleus::escaped_echo_test::printable_ascii;
using nucleus::escaped_echo_test::under;

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
