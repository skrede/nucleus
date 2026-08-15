#include "xml_persist_test_support.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace test = nucleus::xml_persist_test;

TEST_CASE("emit_document + load round-trip preserves a whitespace-only value",
          "[persist][emit][fidelity]")
{
    const nucleus::config_space space = test::port_space();
    for(const std::string &whitespace : {
                std::string(" "), std::string("\t"), std::string("\n")})
    {
        const nucleus::config config = test::checked_config(
                {{"port", whitespace}});
        const auto rendered = nucleus::xml::render_document(config, space);
        REQUIRE(rendered);

        const auto reloaded = nucleus::load_config(
                space, nucleus::source_stack{},
                test::document_options(rendered.value()));
        REQUIRE(reloaded);
        CHECK(reloaded->get("port") == whitespace);
    }
}

TEST_CASE("emit_document + load round-trip preserves a whitespace-only repeated-leaf value",
          "[persist][emit][fidelity]")
{
    const nucleus::config_space space  = test::repeated_tags_space();
    const nucleus::config       config = test::checked_config(
            {{"cluster/tags[0]", " "}, {"cluster/tags[1]", "x"}});
    const auto rendered = nucleus::xml::render_document(config, space);
    REQUIRE(rendered);

    const auto reloaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(rendered.value()));
    REQUIRE(reloaded);
    REQUIRE(reloaded->get_all("cluster/tags") == std::vector<std::string>{" ", "x"});
}
