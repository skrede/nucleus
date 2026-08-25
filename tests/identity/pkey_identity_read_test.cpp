#include "identity/pkey_identity_test_support.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/config_node.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <algorithm>
#include <string_view>

namespace pkey_test = nucleus::pkey_test;

namespace {

bool has_name_child(const nucleus::config &config)
{
    const auto children = config.root()["cluster"]["server"].children();
    return std::any_of(children.begin(), children.end(),
                       [](const nucleus::config_node &child)
                       {
                           return child.path().find("cluster/server/name") !=
                                   std::string_view::npos;
                       });
}

}

TEST_CASE("flat pkey overrides fail loudly", "[pkey_identity][read]")
{
    const nucleus::config_space space = pkey_test::cluster_space();
    nucleus::runtime_source     flat;
    flat.set("cluster/server/name", "tampered");
    nucleus::load_options options;
    options.document_paths = {"doc.xml"};
    options.make_document  = [](const std::string &)
    {
        return pkey_test::xml_of(
                R"(<cluster><server name="web"><port>80</port></server></cluster>)");
    };
    const nucleus::load_result loaded = nucleus::load_config(
            space, nucleus::source_stack{std::move(flat)}, options);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::layering_violation);
    REQUIRE(loaded.error().message.find("cluster/server/name") != std::string::npos);
}

TEST_CASE("pkey leaf is visible in its parent children", "[pkey_identity][read]")
{
    const nucleus::config_space space  = pkey_test::cluster_space();
    const nucleus::load_result  loaded = pkey_test::load_doc(
            space, R"(<cluster><server name="web"><port>80</port></server></cluster>)");
    REQUIRE(loaded);
    REQUIRE(has_name_child(loaded.value()));
}
