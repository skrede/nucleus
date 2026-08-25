#include "xml/persist_test_support.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <filesystem>
#include <string_view>
#include <system_error>
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

TEST_CASE("two live temporary artifacts own directories that cannot collide",
          "[persist][artifact]")
{
    auto first  = test::temporary_artifact::claim("owned.xml");
    auto second = test::temporary_artifact::claim("owned.xml");
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->file().parent_path() != second->file().parent_path());
    CHECK(first->file() != second->file());

    test::write_text(*first, "first");
    test::write_text(*second, "second");
    CHECK(test::checked(first->read()) == "first");
    CHECK(test::checked(second->read()) == "second");

    test::check_step(first->clean_up());
    test::check_step(second->clean_up());
}

TEST_CASE("a claimed candidate directory is retried until a free one is reached",
          "[persist][artifact]")
{
    auto occupied = test::temporary_artifact::claim("unused.xml");
    REQUIRE(occupied);
    const std::filesystem::path taken = occupied->file().parent_path();
    const std::filesystem::path free_path =
            taken.parent_path() / (taken.filename().string() + "-free");

    std::vector<std::filesystem::path> tried;
    const test::candidate_cb           next =
            [&](std::int32_t attempt) -> std::filesystem::path
    {
        tried.push_back(attempt == 0 ? taken : free_path);
        return tried.back();
    };

    auto claimed = test::temporary_artifact::claim("kept.xml", next);
    REQUIRE(claimed);
    CHECK(tried.size() == 2);
    CHECK(tried.front() == taken);
    CHECK(claimed->file().parent_path() == free_path);

    test::check_step(claimed->clean_up());
    test::check_step(occupied->clean_up());
}

TEST_CASE("a candidate the filesystem rejects is reported, never adopted",
          "[persist][artifact]")
{
    std::error_code             code;
    const std::filesystem::path absent =
            std::filesystem::temp_directory_path(code) / "nucleus-absent-parent";
    REQUIRE_FALSE(code);

    const auto claimed = test::temporary_artifact::claim(
            "unreached.xml",
            [&](std::int32_t)
            { return absent / "nucleus-child"; });
    REQUIRE_FALSE(claimed);
    CHECK(claimed.error().find("create_directory") != std::string::npos);
    CHECK(claimed.error().find("nucleus-child") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(absent));
}

TEST_CASE("a destination that disappears fails the write and yields no stale bytes",
          "[persist][artifact]")
{
    auto artifact = test::temporary_artifact::claim("vanishing.xml");
    REQUIRE(artifact);
    test::write_text(*artifact, "stale");
    CHECK(test::checked(artifact->read()) == "stale");

    std::error_code code;
    std::filesystem::remove_all(artifact->file().parent_path(), code);
    REQUIRE_FALSE(code);

    const auto reopened = artifact->open_out();
    REQUIRE_FALSE(reopened);
    CHECK(reopened.error().find("open for write") != std::string::npos);
    CHECK(reopened.error().find("vanishing.xml") != std::string::npos);

    const auto reread = artifact->read();
    REQUIRE_FALSE(reread);
    CHECK(reread.error().find("open for read") != std::string::npos);
}
