#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/config_source/source_stack.h"
#include "nucleus/argv/argv_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Isolated coverage for recognizer_of(space): proves the returned key_recognizer
// answers correctly for known vs unknown paths, and that an explicitly-composed
// argv_source wired via recognize_with(recognizer_of(space)) resolves recognized
// flags and handles unrecognized ones per policy.

// ---------------------------------------------------------------------------
// Direct recognizer_of assertions (independent of argv)
// ---------------------------------------------------------------------------

TEST_CASE("recognizer_of answers true for a declared key and false for an unknown one",
          "[recognizer_of]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_schema("logging/level"));
    REQUIRE(engine.register_schema("server/host"));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto rec = nucleus::recognizer_of(space);

    auto known_level = nucleus::key_path::parse("logging/level").value();
    auto known_host  = nucleus::key_path::parse("server/host").value();
    auto unknown_one = nucleus::key_path::parse("logging/verbosity").value();
    auto unknown_two = nucleus::key_path::parse("database/port").value();

    REQUIRE(rec(known_level));
    REQUIRE(rec(known_host));
    REQUIRE_FALSE(rec(unknown_one));
    REQUIRE_FALSE(rec(unknown_two));
}

// ---------------------------------------------------------------------------
// argv_source explicitly composed with recognizer_of: recognized flags resolve
// ---------------------------------------------------------------------------

TEST_CASE("argv_source composed with recognizer_of resolves a recognized flag via load",
          "[recognizer_of][argv]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_schema("logging/level"));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    // Explicit composition: the argv_source is constructed and wired by the caller,
    // never instantiated automatically by the space.
    nucleus::argv_source argv(std::vector<std::string>{"--logging-level=debug"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(argv)}, {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("logging/level") == "debug");
}

// ---------------------------------------------------------------------------
// Unrecognized flag in strict mode: the schema does not declare the path
// ---------------------------------------------------------------------------

TEST_CASE("argv_source with recognizer_of rejects an undeclared flag in strict mode",
          "[recognizer_of][argv]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_schema("logging/level"));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    // The flag maps to "logging/verbosity" which is NOT in the schema.
    nucleus::argv_source argv(std::vector<std::string>{"--logging-verbosity=3"});
    argv.recognize_with(nucleus::recognizer_of(space))
        .policy(nucleus::unknown_key_policy::strict);

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(argv)}, {});
    REQUIRE_FALSE(loaded);
    // The error must reference the unrecognized path.
    REQUIRE(loaded.error().message.find("logging/verbosity") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Unrecognized flag in lenient mode: the source does not abort on pull()
// ---------------------------------------------------------------------------

TEST_CASE("argv_source with recognizer_of does not abort on an undeclared flag in lenient mode",
          "[recognizer_of][argv]")
{
    // A schema-free space (no elements declared) skips schema enforcement, so
    // the load proves that lenient mode lets the source emit the unknown key
    // without aborting at pull(). When the space has declared elements the
    // schema enforcer would reject the unknown key as undeclared -- that is
    // correct behavior and separate from the lenient/strict source policy.
    nucleus::config_space space = nucleus::builder_result_test::built(nucleus::config_space_builder{});

    nucleus::argv_source argv(std::vector<std::string>{
        "--logging-level=info",
        "--logging-verbosity=3"  // unrecognized by the recognizer
    });
    // recognizer_of on an empty schema answers false for every path.
    argv.recognize_with(nucleus::recognizer_of(space))
        .policy(nucleus::unknown_key_policy::lenient);

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(argv)}, {});
    // Lenient mode: pull() emits both entries without erroring; load proceeds.
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("logging/level") == "info");
    REQUIRE(loaded.value().get("logging/verbosity") == "3");
}

// ---------------------------------------------------------------------------
// Multiple declared paths: each is recognized, non-declared paths are rejected
// ---------------------------------------------------------------------------

TEST_CASE("recognizer_of distinguishes multiple declared paths from non-declared ones",
          "[recognizer_of]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_schema("db/host"));
    REQUIRE(engine.register_schema("db/port"));
    REQUIRE(engine.register_schema("auth/token"));
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto rec = nucleus::recognizer_of(space);

    REQUIRE(rec(nucleus::key_path::parse("db/host").value()));
    REQUIRE(rec(nucleus::key_path::parse("db/port").value()));
    REQUIRE(rec(nucleus::key_path::parse("auth/token").value()));

    REQUIRE_FALSE(rec(nucleus::key_path::parse("db/name").value()));
    REQUIRE_FALSE(rec(nucleus::key_path::parse("auth/secret").value()));
    REQUIRE_FALSE(rec(nucleus::key_path::parse("logging/level").value()));
}
