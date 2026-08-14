#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <filesystem>

#ifndef NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR
    #error "NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR must be defined by the build"
#endif

namespace {

nucleus::config_space server_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    return builder.build();
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

std::string routes(std::size_t count, const std::string &prefix)
{
    std::string text;
    for(std::size_t ordinal = 0; ordinal < count; ++ordinal)
        text += "<route><port>" + prefix + "-port-" + std::to_string(ordinal) + "</port><method>" + prefix + "-method-" + std::to_string(ordinal) + "</method></route>";
    return text;
}

std::string base_document(std::size_t count)
{
    return "<cluster><server name=\"primary\">" + routes(count, "base") + "</server></cluster>";
}

std::string derived_document(std::size_t count)
{
    return "<cluster inherit=\"base.xml\"><server name=\"primary\" extend=\"narrow\">" + routes(count, "derived") + "</server></cluster>";
}

nucleus::load_result load_narrow_extend(const nucleus::config_space &space)
{
    const std::filesystem::path root(NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(root / "strain_narrow_extend"));
    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document  = nucleus::shapes::file_factory(
            (root / "strain_narrow_extend").string());
    opts.selection = "primary";
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

nucleus::load_result load_sweep(const nucleus::config_space &space,
                                std::size_t total, std::size_t supplied)
{
    const std::string     base    = base_document(total);
    const std::string     derived = derived_document(supplied);
    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document  = [base, derived](const std::string &path)
    {
        return xml_of(nucleus::shapes::filename_of(path) == "base.xml" ? base : derived);
    };
    opts.selection = "primary";
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

void require_base_origin(const nucleus::config &cfg, const std::string &path)
{
    const nucleus::origin *origin = cfg.provenance_of(path);
    REQUIRE(origin != nullptr);
    REQUIRE(origin->rank == 0);
    REQUIRE(origin->layer == "path:base.xml");
    REQUIRE_FALSE(origin->owner.has_value());
    REQUIRE(origin->inheritance_layer == 0);
}

void require_sweep(const nucleus::config &cfg, std::size_t total,
                   std::size_t supplied)
{
    const std::size_t survivors = total - supplied;
    for(std::size_t target = 0; target < survivors; ++target)
    {
        const std::size_t source = target + supplied;
        const std::string route  = "cluster/server/route[" + std::to_string(target) + "]";
        REQUIRE(cfg.get(route + "/port") == "base-port-" + std::to_string(source));
        REQUIRE(cfg.get(route + "/method") == "base-method-" + std::to_string(source));
        require_base_origin(cfg, route + "/port");
        require_base_origin(cfg, route + "/method");
    }
    const std::string absent = "cluster/server/route[" + std::to_string(survivors) + "]";
    REQUIRE_FALSE(cfg.contains(absent + "/port"));
    REQUIRE_FALSE(cfg.contains(absent + "/method"));
}

}

TEST_CASE("a narrow extend closes the ordinal gap while preserving the surviving route "
          "and its complete origin",
          "[collection_shapes][keyed][compaction]")
{
    const nucleus::config_space space  = server_space();
    const nucleus::load_result  loaded = load_narrow_extend(space);
    REQUIRE(loaded);
    const nucleus::config &cfg        = loaded.value();
    const std::string      serialized = nucleus::shapes::serialize(cfg);
    INFO(serialized);
    REQUIRE(cfg.get("cluster/server/route[0]/port") == "443");
    REQUIRE(cfg.get("cluster/server/route[0]/method") == "post");
    REQUIRE_FALSE(cfg.contains("cluster/server/route[1]/port"));
    REQUIRE_FALSE(cfg.contains("cluster/server/route[1]/method"));
    REQUIRE(serialized.find("cluster/server/route[0]/port = 443 "
                            "[0|path:base.xml|anonymous|0]\n") != std::string::npos);
    REQUIRE(serialized.find("cluster/server/route[0]/method = post "
                            "[0|path:base.xml|anonymous|0]\n") != std::string::npos);
}

TEST_CASE("narrow extensions compact every removed prefix across an ordinal sweep",
          "[collection_shapes][keyed][compaction][sweep]")
{
    const nucleus::config_space space = server_space();
    for(std::size_t total = 2; total <= 12; ++total)
        for(std::size_t supplied = 1; supplied < total; ++supplied)
        {
            CAPTURE(total, supplied);
            const nucleus::load_result loaded = load_sweep(space, total, supplied);
            REQUIRE(loaded);
            require_sweep(loaded.value(), total, supplied);
        }
}
