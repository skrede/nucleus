#include "nucleus/config_space.h"

#include "support/builder_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("document paths named with no parser factory are refused, not skipped",
          "[policy][load]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_schema("k"));
    nucleus::config_space space = nucleus::builder_result_test::built(builder);

    nucleus::load_options options;
    options.document_paths = {"config.xml"};

    const auto loaded = nucleus::load_config(space, nucleus::source_stack{}, options);
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().message.find("config.xml") != std::string::npos);

    // The capability pre-flight consumes the same option and must refuse alike.
    CHECK_FALSE(nucleus::check_capabilities(space, nucleus::source_stack{}, options));
}
