#include "collection_shapes.h"

#include "nucleus/config.h"
#include "../builder_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <filesystem>

#ifndef NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR
#error "NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR must be defined by the build"
#endif

namespace {

nucleus::config_space server_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    return nucleus::builder_result_test::built(builder);
}

// derived.xml inherits from base.xml and introduces a second, different strain, so
// one load carries both strains through the sweep before slice picks one.
nucleus::load_result load_strain(const nucleus::config_space &space,
                                 const std::string &strain)
{
    const std::filesystem::path root(NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(root / "keyed_and_ordinal"));

    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document = nucleus::shapes::file_factory((root / "keyed_and_ordinal").string());
    opts.selection = strain;
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

}

TEST_CASE("a later layer introducing a second strain leaves the earlier strain's ordinal "
          "routes intact",
          "[collection_shapes][keyed][ordinal]")
{
    const nucleus::config_space space = server_space();

    const nucleus::load_result loaded = load_strain(space, "alpha");
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));

    REQUIRE(loaded.value().get("cluster/server/route[0]/port") == "80");
    REQUIRE(loaded.value().get("cluster/server/route[1]/port") == "443");
    REQUIRE(loaded.value().get("cluster/server/route[1]/method") == "post");
    REQUIRE_FALSE(loaded.value().contains("cluster/server/beta/route"));
}

TEST_CASE("selecting the later strain sees both of its own ordinal routes and none of "
          "the earlier strain's",
          "[collection_shapes][keyed][ordinal]")
{
    const nucleus::config_space space = server_space();

    const nucleus::load_result loaded = load_strain(space, "beta");
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));

    REQUIRE(loaded.value().get("cluster/server/route[0]/port") == "8080");
    REQUIRE(loaded.value().get("cluster/server/route[1]/port") == "9090");
    REQUIRE(loaded.value().get_all("cluster/server/route/method")
            == std::vector<std::string>{"put", "head"});
    REQUIRE_FALSE(loaded.value().contains("cluster/server/alpha/route"));
}
