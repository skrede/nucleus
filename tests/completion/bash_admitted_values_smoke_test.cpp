#include "completion/bash_test_support.h"
#include "completion/admitted_values_test_support.h"

#include "support/escaped_echo_test_support.h"
#include "support/builder_result_test_support.h"
#include "support/completion_result_test_support.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using nucleus::admitted_values_test::admitted;
using nucleus::admitted_values_test::admitted_bytes;
using nucleus::bash_test::bash_available;
using nucleus::bash_test::insert_and_parse;
using nucleus::bash_test::run_seeded;
using nucleus::builder_result_test::built;
using nucleus::completion_result_test::script_of;
using nucleus::escaped_echo_test::under;

TEST_CASE("the registration rule admits 191 single-byte allowed values, neither a newline nor a NUL",
          "[completion][registration]")
{
    CHECK(admitted_bytes().size() == 191);
    CHECK_FALSE(admitted("\n"));
    CHECK_FALSE(admitted(std::string(1, '\0')));
}

TEST_CASE("every allowed value the registration rule admits round-trips through the generated bash script",
          "[completion][smoke]")
{
    if(!bash_available())
        SKIP("no bash 5.0 or later on PATH");

    const std::vector<std::string> values = admitted_bytes();
    nucleus::config_space_builder  engine;
    REQUIRE(engine.register_element(nucleus::element("app", nucleus::anchor::root())));
    REQUIRE(engine.register_element(nucleus::enum_element("mode", under("app"), values)));
    const std::string completion = script_of(built(engine).generate_completion(nucleus::shell::bash, "myapp"));

    std::string expected;
    for(const std::string &value : values)
        expected += "<--app-mode=" + value + ">\n";
    const std::string drive = insert_and_parse({"--app-mode", "="}, "--app-mode=", "");
    REQUIRE(run_seeded(completion, drive, {}) == expected);
    REQUIRE(run_seeded("export LC_ALL=C\n" + completion, drive, {}) == expected);
}
