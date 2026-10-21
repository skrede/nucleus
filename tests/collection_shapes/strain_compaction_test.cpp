#include "collection_shapes.h"

#include "nucleus/config.h"
#include "support/builder_result_test_support.h"
#include "nucleus/strain_scope.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <sstream>
#include <filesystem>

#ifndef NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR
    #error "NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR must be defined by the build"
#endif

namespace {

struct expected_route
{
    std::string port;
    std::string method;
    std::string origin;
};

const std::string base_origin    = "[0|path:base.xml|anonymous|0]";
const std::string derived_origin = "[1|path:derived.xml|anonymous|1]";

nucleus::config_space server_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    return nucleus::builder_result_test::built(builder);
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

std::string route_of(const std::string &port, const std::string &method)
{
    return "<route><port>" + port + "</port><method>" + method + "</method></route>";
}

std::string routes(std::size_t count, const std::string &prefix)
{
    std::string text;
    for(std::size_t ordinal = 0; ordinal < count; ++ordinal)
        text += route_of(prefix + "-port-" + std::to_string(ordinal), prefix + "-method-" + std::to_string(ordinal));
    return text;
}

std::string derived_document(const std::string &extend, const std::string &content)
{
    return "<cluster inherit=\"base.xml\"><server name=\"primary\" extend=\"" + extend + "\">" + content + "</server></cluster>";
}

nucleus::load_result load_narrow_extend(const nucleus::config_space &space,
                                        nucleus::strain_scope_policy policy =
                                                nucleus::strain_scope_policy::space_open_container_closed)
{
    const std::filesystem::path root(NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(root / "strain_narrow_extend"));
    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document  = nucleus::shapes::file_factory(
            (root / "strain_narrow_extend").string());
    opts.selection = "primary";
    opts.scope     = policy;
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

nucleus::load_result reload_emitted(const nucleus::config_space &space,
                                    const nucleus::config       &config)
{
    std::ostringstream emitted;
    REQUIRE(nucleus::xml::emit_document(config, space, emitted));
    nucleus::load_options options;
    options.selection = "primary";
    return nucleus::load_config(
            space, nucleus::source_stack{xml_of(emitted.str())}, options);
}

nucleus::load_result load_documents(const nucleus::config_space &space, const std::string &base,
                                    const std::string &derived, nucleus::strain_scope_policy scope)
{
    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document  = [base, derived](const std::string &path)
    {
        return xml_of(nucleus::shapes::filename_of(path) == "base.xml" ? base : derived);
    };
    opts.selection = "primary";
    opts.scope     = scope;
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

void require_routes(const nucleus::load_result &loaded, const std::vector<expected_route> &resolved)
{
    REQUIRE(loaded);
    const std::string serialized = nucleus::shapes::serialize(loaded.value());
    INFO(serialized);
    REQUIRE(serialized.find("cluster/server/name = primary " + derived_origin + "\n") != std::string::npos);
    for(std::size_t ordinal = 0; ordinal < resolved.size(); ++ordinal)
    {
        const std::string route            = "cluster/server/route[" + std::to_string(ordinal) + "]";
        const auto &[port, method, origin] = resolved[ordinal];
        REQUIRE(serialized.find(route + "/port = " + port + " " + origin + "\n") != std::string::npos);
        REQUIRE(serialized.find(route + "/method = " + method + " " + origin + "\n") != std::string::npos);
    }
    const std::string absent = "cluster/server/route[" + std::to_string(resolved.size()) + "]";
    REQUIRE_FALSE(loaded->contains(absent + "/port"));
    REQUIRE_FALSE(loaded->contains(absent + "/method"));
}

std::vector<expected_route> base_routes(std::size_t total)
{
    std::vector<expected_route> resolved;
    for(std::size_t ordinal = 0; ordinal < total; ++ordinal)
        resolved.push_back({"base-port-" + std::to_string(ordinal), "base-method-" + std::to_string(ordinal), base_origin});
    return resolved;
}

}

TEST_CASE("a narrow extend the default policy excludes leaves the parent's routes in place",
          "[collection_shapes][keyed][compaction]")
{
    require_routes(load_narrow_extend(server_space()),
                   {{"80", "get", base_origin}, {"443", "post", base_origin}});
}

TEST_CASE("a compacted narrow extension retains its identity through XML round trip",
          "[collection_shapes][keyed][compaction][round_trip]")
{
    const nucleus::config_space space = server_space();
    const nucleus::load_result  first = load_narrow_extend(space);
    REQUIRE(first);
    const nucleus::load_result second = reload_emitted(space, first.value());
    REQUIRE(second);
    REQUIRE(second->keys() == first->keys());
    for(const std::string &key : first->keys())
        REQUIRE(second->get(key) == first->get(key));
}

TEST_CASE("narrow extensions leave every parent route in place across an ordinal sweep",
          "[collection_shapes][keyed][compaction][sweep]")
{
    const nucleus::config_space space = server_space();
    for(std::size_t total = 2; total <= 12; ++total)
        for(std::size_t supplied = 1; supplied < total; ++supplied)
        {
            CAPTURE(total, supplied);
            const std::string base = "<cluster><server name=\"primary\">" + routes(total, "base") + "</server></cluster>";
            require_routes(load_documents(space, base, derived_document("narrow", routes(supplied, "derived")),
                                          nucleus::strain_scope_policy::space_open_container_closed),
                           base_routes(total));
        }
}

TEST_CASE("file-level narrow inheritance retains the selected identity",
          "[collection_shapes][keyed][compaction][scope]")
{
    require_routes(load_narrow_extend(server_space(), nucleus::strain_scope_policy::file_level),
                   {{"80", "get", base_origin}, {"443", "post", base_origin}});
}

TEST_CASE("a narrow extend composes its route where the scope policy opens the container",
          "[collection_shapes][keyed][compaction][scope]")
{
    require_routes(load_narrow_extend(server_space(), nucleus::strain_scope_policy::container_open_until_next_strain),
                   {{"8080", "put", derived_origin}, {"443", "post", base_origin}});
}

TEST_CASE("a wide extend composes its route under every scope policy",
          "[collection_shapes][keyed][compaction][scope]")
{
    const nucleus::config_space space = server_space();
    const std::string           base  = "<cluster><server name=\"primary\">" + route_of("80", "get") + route_of("443", "post") + "</server></cluster>";
    for(const nucleus::strain_scope_policy scope : {nucleus::strain_scope_policy::file_level,
                                                    nucleus::strain_scope_policy::space_open_container_closed,
                                                    nucleus::strain_scope_policy::container_open_until_next_strain})
    {
        CAPTURE(scope);
        require_routes(load_documents(space, base, derived_document("wide", route_of("8080", "put")), scope),
                       {{"8080", "put", derived_origin}, {"443", "post", base_origin}});
    }
}
