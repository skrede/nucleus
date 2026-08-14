#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

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

std::string three_route_document()
{
    return "<cluster><server name=\"primary\">"
           "<route><port>80</port><method>get</method></route>"
           "<route><port>443</port><method>post</method></route>"
           "<route><port>8443</port><method>head</method></route>"
           "</server></cluster>";
}

nucleus::load_result load_document(const nucleus::config_space &space)
{
    nucleus::load_options opts;
    opts.selection = "primary";
    return nucleus::load_config(
            space, nucleus::source_stack{xml_of(three_route_document())}, opts);
}

nucleus::load_result load_overlay(const nucleus::config_space &space)
{
    nucleus::load_options opts;
    opts.selection = "primary";
    return nucleus::load_config(
            space,
            nucleus::source_stack{xml_of(three_route_document()),
                                  nucleus::shapes::runtime_layer(
                                          {{"cluster/server/route[1]/method", "patch"}})},
            opts);
}

void require_document_order(const nucleus::config &cfg, const std::string &middle)
{
    REQUIRE(cfg.get("cluster/server/route[0]/method") == "get");
    REQUIRE(cfg.get("cluster/server/route[0]/port") == "80");
    REQUIRE(cfg.get("cluster/server/route[1]/method") == middle);
    REQUIRE(cfg.get("cluster/server/route[1]/port") == "443");
    REQUIRE(cfg.get("cluster/server/route[2]/method") == "head");
    REQUIRE(cfg.get("cluster/server/route[2]/port") == "8443");
}

}

TEST_CASE("document order survives a runtime override of the middle instance",
          "[collection_shapes][keyed][compaction][ordering]")
{
    const nucleus::config_space space   = server_space();
    const nucleus::load_result  direct  = load_document(space);
    const nucleus::load_result  layered = load_overlay(space);
    REQUIRE(direct);
    REQUIRE(layered);
    INFO(nucleus::shapes::serialize(direct.value()));
    INFO(nucleus::shapes::serialize(layered.value()));
    require_document_order(direct.value(), "post");
    require_document_order(layered.value(), "patch");
}

TEST_CASE("two single-document loads of a contiguous collection have identical values "
          "and origins",
          "[collection_shapes][keyed][compaction][idempotency]")
{
    const nucleus::config_space space  = server_space();
    const nucleus::load_result  first  = load_document(space);
    const nucleus::load_result  second = load_document(space);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(nucleus::shapes::serialize(first.value()) == nucleus::shapes::serialize(second.value()));
}

TEST_CASE("two layered loads of a contiguous collection have identical values and origins",
          "[collection_shapes][keyed][compaction][idempotency]")
{
    const nucleus::config_space space  = server_space();
    const nucleus::load_result  first  = load_overlay(space);
    const nucleus::load_result  second = load_overlay(space);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(nucleus::shapes::serialize(first.value()) == nucleus::shapes::serialize(second.value()));
}
