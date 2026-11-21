#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/error.h"
#include "nucleus/config_space.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

nucleus::key_path path_of(const std::string &text)
{
    auto parsed = nucleus::key_path::parse(text);
    REQUIRE(parsed);
    return parsed.value();
}

}

TEST_CASE("a typed schema element registers through the builder", "[facade][schema]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::element("level", nucleus::anchor::keyspace(path_of("logging")))));

    // Referential integrity is enforced through the builder too: an element under
    // an undefined keyspace is rejected.
    auto bad = engine.register_element(
        nucleus::element("mode", nucleus::anchor::keyspace(path_of("absent"))));
    REQUIRE_FALSE(bad);
}

TEST_CASE("an element name that is not a single clean path segment is refused at registration",
          "[facade][schema]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));

    const std::vector<std::string> malformed{
        "", "logging/level", "level[0]", "level]", "<level>", "lev\x7f" "el", "log$(id)x",
        "log`id`x", "log;x", "log|x", "log&x", "log'x", "log\"x", "log(x", "log~x", "log{x}",
        "log:x", "log*x", "log?x", "log#x", "log!x", "log\\x", "log%x"};
    for(const std::string &name : malformed)
    {
        auto refused = engine.register_element(
            nucleus::element(name, nucleus::anchor::keyspace(path_of("logging"))));
        REQUIRE_FALSE(refused);
        REQUIRE(refused.error().code == nucleus::errc::rejected_registration);
        REQUIRE(refused.error().message.find("malformed name") != std::string::npos);
    }

    // The offending name reaches the diagnostic escaped, so a newline carried in it
    // cannot forge a second log line out of the message it triggered.
    auto newline = engine.register_element(
        nucleus::element("lev\nel", nucleus::anchor::keyspace(path_of("logging"))));
    REQUIRE_FALSE(newline);
    REQUIRE(newline.error().message.find("lev\\nel") != std::string::npos);
    REQUIRE(newline.error().message.find('\n') == std::string::npos);

    // '=' stays admitted: it is the one unrenderable key a schema can still declare.
    for(const char *admitted : {"log_level.2", "bad=key", "log+x", "log,x"})
        REQUIRE(engine.register_element(
            nucleus::element(admitted, nucleus::anchor::keyspace(path_of("logging")))));
}

TEST_CASE("a declared name carrying a space is refused at registration", "[facade][schema]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));

    auto spaced = engine.register_element(
        nucleus::element("my flag", nucleus::anchor::keyspace(path_of("logging"))));
    REQUIRE_FALSE(spaced);
    REQUIRE(spaced.error().code == nucleus::errc::rejected_registration);
    REQUIRE(spaced.error().message.find("malformed name") != std::string::npos);
    REQUIRE(spaced.error().message.find("my flag") != std::string::npos);

    // A space is printable and reaches the diagnostic verbatim; a tab does not, which is what
    // tells the two refusals apart in the one message they share.
    auto tabbed = engine.register_element(
        nucleus::element("my\tflag", nucleus::anchor::keyspace(path_of("logging"))));
    REQUIRE_FALSE(tabbed);
    REQUIRE(tabbed.error().message.find("my\\tflag") != std::string::npos);
    REQUIRE(tabbed.error().message.find('\t') == std::string::npos);
}
