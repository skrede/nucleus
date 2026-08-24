#include "collection_shapes.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "../builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/cli_surface.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <filesystem>

#ifndef NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR
    #error "NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR must be defined by the build"
#endif

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

nucleus::config_space strain_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space merge_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("endpoints", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::merging(
            nucleus::repeated_element("output", nucleus::anchor::keyspace("endpoints")),
            nucleus::merge_mode::replace_by_key)));
    REQUIRE(builder.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("endpoints/output"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_names", nucleus::anchor::keyspace("endpoints"))
                    .members({"output"})
                    .field("name")));
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space validation_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("output", nucleus::anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("cluster/node/output"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("route", nucleus::anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            nucleus::unique_element("port", nucleus::anchor::keyspace("cluster/node/route"))));
    REQUIRE(builder.register_element(
            nucleus::unique_element("method", nucleus::anchor::keyspace("cluster/node/route"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_names", nucleus::anchor::keyspace("cluster/node"))
                    .members({"output"})
                    .field("name")));
    return nucleus::builder_result_test::built(builder);
}

nucleus::load_options strain_options()
{
    const std::filesystem::path root(NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(root / "strain_narrow_extend"));
    nucleus::load_options options;
    options.document_paths = {"derived.xml"};
    options.make_document  = nucleus::shapes::file_factory(
            (root / "strain_narrow_extend").string());
    options.selection = "primary";
    return options;
}

bool mentions(const nucleus::error &error, const char *text)
{
    return error.message.find(text) != std::string::npos;
}

}

TEST_CASE("duplicate primary keys in one document carry the malformed source code",
          "[collection_shapes][error_channel]")
{
    const nucleus::load_result loaded = nucleus::load_config(
            strain_space(), nucleus::source_stack{xml_of("<cluster><server name=\"alpha\"><route><port>80</port></route></server>"
                                                         "<server name=\"alpha\"><route><port>443</port></route></server></cluster>")},
            {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::malformed_source);
    REQUIRE(mentions(loaded.error(), "duplicate primary-key value 'alpha'"));
    REQUIRE(mentions(loaded.error(), "container 'cluster/server'"));
}

TEST_CASE("same-layer keyed duplicates carry the layering violation code",
          "[collection_shapes][error_channel]")
{
    const nucleus::load_result loaded = nucleus::load_config(
            merge_space(), nucleus::source_stack{xml_of("<endpoints><output><name>alpha</name></output>"
                                                        "<output><name>alpha</name></output></endpoints>")},
            {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
    REQUIRE(mentions(loaded.error(), "identifier 'name'='alpha'"));
    REQUIRE(mentions(loaded.error(), "within layer 'stack[0]'"));
}

TEST_CASE("duplicate identities in one enclosing instance carry the schema violation code",
          "[collection_shapes][error_channel]")
{
    const nucleus::load_result loaded = nucleus::load_config(
            validation_space(), nucleus::source_stack{xml_of("<cluster><node><output><name>alpha</name></output>"
                                                             "<output><name>alpha</name></output></node></cluster>")},
            {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(mentions(loaded.error(), "cluster/node[0]/output[0]/name"));
    REQUIRE(mentions(loaded.error(), "cluster/node[0]/output[1]/name"));
}

TEST_CASE("multiple validation violations share one code and retain both reasons",
          "[collection_shapes][error_channel]")
{
    const nucleus::load_result loaded = nucleus::load_config(
            validation_space(), nucleus::source_stack{xml_of("<cluster><node><route><port>80</port><method>get</method></route>"
                                                             "<route><port>80</port><method>get</method></route></node></cluster>")},
            {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message ==
            "schema validation failed:\n"
            "  - unique field 'cluster/node/route/port' has duplicate value '80' across sibling instances "
            "'cluster/node[0]/route[0]/port', 'cluster/node[0]/route[1]/port'\n"
            "  - unique field 'cluster/node/route/method' has duplicate value 'get' across sibling instances "
            "'cluster/node[0]/route[0]/method', 'cluster/node[0]/route[1]/method'");
}

TEST_CASE("command-line ordinals address post-compaction collection slots",
          "[collection_shapes][error_channel]")
{
    const nucleus::config_space space = strain_space();

    nucleus::argv_source valid({"--cluster-server-route-0-method=patched"});
    valid.recognize_with(nucleus::recognizer_of(space));
    const nucleus::load_result loaded = nucleus::load_config(
            space, nucleus::source_stack{std::move(valid)}, strain_options());
    REQUIRE(loaded);
    REQUIRE(loaded->get("cluster/server/route[0]/port") == "443");
    REQUIRE(loaded->get("cluster/server/route[0]/method") == "patched");
    REQUIRE_FALSE(loaded->contains("cluster/server/route[1]/port"));

    nucleus::argv_source invalid({"--cluster-server-route-1-method=patched"});
    invalid.recognize_with(nucleus::recognizer_of(space));
    const nucleus::load_result rejected = nucleus::load_config(
            space, nucleus::source_stack{std::move(invalid)}, strain_options());
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().code == nucleus::errc::schema_violation);
    REQUIRE(mentions(rejected.error(),
                     "argv ordinal 1 for 'cluster/server/route' is out of range: "
                     "1 instance(s) exist; out of range"));
}
