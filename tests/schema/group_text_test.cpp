#include "support/escaped_echo_test_support.h"

#include "nucleus/error.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/constraint_group.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>

namespace {

using nucleus::escaped_echo_test::framed;
using nucleus::escaped_echo_test::require_escaped_echo;
using nucleus::escaped_echo_test::terminal_controls;
using nucleus::escaped_echo_test::under;

nucleus::config_space_builder logging_with_leaf()
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("logging", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("a", under("logging"))));
    return engine;
}

std::string refusal_message(const nucleus::registration_result &result)
{
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::rejected_registration);
    return result.error().message;
}

std::string constraint_refusal(std::string name, nucleus::anchor at, std::vector<std::string> members)
{
    nucleus::config_space_builder engine = logging_with_leaf();
    return refusal_message(engine.register_constraint_group(
            nucleus::exclusion_group(std::move(name), std::move(at)).members(std::move(members)).at_most(1)));
}

std::string identity_refusal(std::string name, nucleus::anchor at, std::vector<std::string> members, std::string field)
{
    nucleus::config_space_builder engine = logging_with_leaf();
    return refusal_message(engine.register_identity_group(
            nucleus::identity_group(std::move(name), std::move(at)).members(std::move(members)).field(std::move(field))));
}

}

TEST_CASE("a group registration refusal echoes the control-carrying text it refuses escaped", "[schema][registration]")
{
    for(const std::string &control : terminal_controls)
    {
        const std::string     text      = framed(control);
        const std::string     undefined = "nowhere" + control;
        const std::string     reserved  = "nucleus" + control;
        const nucleus::anchor logging   = under("logging");
        const nucleus::anchor nowhere   = nucleus::anchor::keyspace(undefined);
        require_escaped_echo(constraint_refusal(text, logging, {}), "declares no members", text);
        require_escaped_echo(constraint_refusal("g", logging, {text}), "is not a declared element under", text);
        require_escaped_echo(constraint_refusal("g", nowhere, {"a"}), "cannot anchor under undefined keyspace", undefined);
        require_escaped_echo(identity_refusal(text, logging, {}, "id"), "declares no members", text);
        require_escaped_echo(identity_refusal(text, logging, {"a"}, ""), "declares no identifier field", text);
        require_escaped_echo(identity_refusal("h", logging, {text}, "id"), "has no declared identifier field", text);
        require_escaped_echo(identity_refusal("h", logging, {"a"}, text), "has no declared identifier field", text);
        require_escaped_echo(identity_refusal("h", nowhere, {"a"}, "id"), "cannot anchor under undefined keyspace", undefined);
        require_escaped_echo(identity_refusal(reserved, logging, {"a"}, "id"), "collides with a reserved name", reserved);
    }
}
