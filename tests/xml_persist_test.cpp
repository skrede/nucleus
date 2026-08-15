#include "xml_persist_test_support.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>
#include <string_view>
#include <initializer_list>

namespace test = nucleus::xml_persist_test;

namespace {

void check_malformed(
        std::string                                   document,
        const std::initializer_list<std::string_view> diagnostics)
{
    const nucleus::config_space space  = test::server_space();
    const auto                  loaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(std::move(document)));
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::malformed_source);
    for(const std::string_view diagnostic : diagnostics)
        CHECK(loaded.error().message.find(diagnostic) != std::string::npos);
}

}

TEST_CASE("a resolved config round-trips through XML persistence", "[persist]")
{
    const nucleus::config_space space = test::server_space();
    const auto                  first = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(test::server_document));
    REQUIRE(first);

    const auto rendered = nucleus::xml::render_document(first.value(), space);
    REQUIRE(rendered);
    const auto second = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(rendered.value()));
    REQUIRE(second);

    REQUIRE(second->keys() == first->keys());
    for(const std::string &key : first->keys())
        REQUIRE(second->get_all(key) == first->get_all(key));
    REQUIRE(first->get_all("server/tag") == std::vector<std::string>{"alpha", "beta"});
    REQUIRE(second->get_all("server/tag") == std::vector<std::string>{"alpha", "beta"});
}

TEST_CASE("a duplicate attribute is rejected as malformed_source",
          "[persist][malformed]")
{
    check_malformed(
            "<server host=\"a\" host=\"b\"><host>c</host></server>",
            {"host", "server"});
}

TEST_CASE("more than one root element is rejected as malformed_source",
          "[persist][malformed]")
{
    check_malformed(
            "<server><host>a</host></server>"
            "<server><host>b</host></server>",
            {"more than one root"});
}

TEST_CASE("trailing content after the root element is rejected as malformed_source",
          "[persist][malformed]")
{
    check_malformed(
            "<server><host>a</host></server><![CDATA[trailing]]>",
            {"outside the root element"});
}

TEST_CASE("content before the root element is rejected as malformed_source",
          "[persist][malformed]")
{
    check_malformed(
            "<![CDATA[leading]]><server><host>a</host></server>",
            {"outside the root element"});
}

TEST_CASE("an attribute-bearing text element is rejected as mixed content",
          "[persist][malformed]")
{
    check_malformed(
            "<server><host attr=\"x\">8080</host></server>",
            {"mixes character data"});
}

TEST_CASE("a hand-written single-root leaf reads its own text on the unnamed path",
          "[persist][fidelity]")
{
    const nucleus::config_space space  = test::port_space();
    const auto                  loaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options("<port>8080</port>"));
    REQUIRE(loaded);
    REQUIRE(loaded->get("port") == "8080");
}
