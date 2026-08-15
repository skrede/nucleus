#include "xml_repeated_test_support.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace test = nucleus::xml_repeated_test;

TEST_CASE("emit still writes a repeated scalar leaf value", "[xml][xml_emitter]")
{
    const nucleus::config_space space  = test::repeated_tags_space();
    const nucleus::config       config = test::checked_config(
            {{"cluster/tags[0]", "a"}, {"cluster/tags[1]", "b"}});
    const auto rendered = nucleus::xml::render_document(config, space);
    REQUIRE(rendered);
    REQUIRE(rendered->find(">a<") != std::string::npos);
    REQUIRE(rendered->find(">b<") != std::string::npos);
}

TEST_CASE("a whitespace-only leaf value under a repeated container round-trips",
          "[xml][repeated_container][round_trip][fidelity]")
{
    const nucleus::config_space space  = test::cluster_space();
    const nucleus::config       config = test::checked_config(
            {{"cluster/node[0]/port", " "}});
    const auto rendered = nucleus::xml::render_document(config, space);
    REQUIRE(rendered);
    const auto reloaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(rendered.value()));
    REQUIRE(reloaded);
    CHECK(reloaded->get("cluster/node[0]/port") == " ");
}

TEST_CASE("xml emitter -- contiguous indexed leaves round-trip in order",
          "[xml][xml_emitter][round_trip]")
{
    const nucleus::config_space space    = test::repeated_tags_space();
    const nucleus::config       original = test::checked_config(
            {{"cluster/tags[0]", "a"}, {"cluster/tags[1]", "b"}});
    const auto rendered = nucleus::xml::render_document(original, space);
    REQUIRE(rendered);
    const auto reloaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(rendered.value()));
    REQUIRE(reloaded);
    REQUIRE(reloaded->get_all("cluster/tags") ==
            std::vector<std::string>{"a", "b"});
}
