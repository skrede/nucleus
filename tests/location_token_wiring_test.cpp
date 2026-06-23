#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/xml/xml_source.h"

#include "nucleus/config_source/source_stack.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <filesystem>
#include <string>

// Acceptance test.
// Two XML files in DIFFERENT directories each use ${dir.path}. After load each
// value must resolve to that file's OWN directory, not the entrypoint directory.
// This proves per-source file-frame binding (the assemble_handles origin_file wiring).

namespace fs = std::filesystem;
using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::load_options;
using nucleus::source_handle;
using nucleus::xml_source;
using nucleus::xml_source_options;

namespace {

// Writes `content` to `path`, creating parent directories as needed.
void write_file(const fs::path &path, const std::string &content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << content;
}

}

TEST_CASE("Two files in different directories resolve ${dir.path} to their own directory",
          "[location][loc-01][xml]")
{
    // Set up two temp directories under the system temp root.
    const fs::path tmp = fs::temp_directory_path();
    const fs::path dir_a = tmp / "nucleus_loc01_test_a";
    const fs::path dir_b = tmp / "nucleus_loc01_test_b";

    const fs::path file_a = dir_a / "primary.xml";
    const fs::path file_b = dir_b / "base.xml";

    // primary.xml inherits base.xml and declares its own ${dir.path} field.
    const std::string xml_a =
        "<config inherit=\"" + file_b.string() + "\">"
        "<primary_dir>${dir.path}</primary_dir>"
        "</config>";

    // base.xml has no inherit; declares its own ${dir.path} field.
    const std::string xml_b =
        "<config>"
        "<base_dir>${dir.path}</base_dir>"
        "</config>";

    write_file(file_a, xml_a);
    write_file(file_b, xml_b);

    // Cleanup on scope exit.
    struct cleanup_guard
    {
        const fs::path &da;
        const fs::path &db;
        ~cleanup_guard()
        {
            std::error_code ec;
            fs::remove_all(da, ec);
            fs::remove_all(db, ec);
        }
    } guard{dir_a, dir_b};

    auto space = config_space_builder{}.build();

    load_options opts;
    opts.document_paths = {file_a.string()};
    opts.make_document = [&](const std::string &path) -> source_handle {
        return source_handle(xml_source::from(xml_source_options::of_file(path)));
    };

    auto loaded = load_config(space, nucleus::source_stack{}, opts);
    REQUIRE(loaded.has_value());

    const auto &cfg = loaded.value();

    // The XML root element <config> acts as the top-level keyspace prefix.
    // primary_dir came from primary.xml (dir_a); its ${dir.path} must equal dir_a.
    const auto primary_dir_val = cfg.get("config/primary_dir");
    REQUIRE(primary_dir_val.has_value());
    CHECK(fs::path(*primary_dir_val) == dir_a);

    // base_dir came from base.xml (dir_b); its ${dir.path} must equal dir_b.
    const auto base_dir_val = cfg.get("config/base_dir");
    REQUIRE(base_dir_val.has_value());
    CHECK(fs::path(*base_dir_val) == dir_b);
}
