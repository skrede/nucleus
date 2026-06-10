#include "golden_runner.h"

#include "nucleus/configuration_space.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <optional>
#include <filesystem>

// Data-driven golden harness: discovers every fixture case directory under the
// compile-time fixture root, drives its document stack through load,
// and asserts the serialized resolved keyspace matches the pre-committed golden
// exactly. One fixture is a deliberate divergence whose case.txt sets
// expect_divergence=true; for it the harness asserts the diff routine REPORTS a
// mismatch, proving detection works on a real diff. This ctest target runs in the
// default CI set (NUCLEUS_BUILD_SOURCE_XML=ON), so any divergence blocks merge.

#ifndef NUCLEUS_GOLDEN_FIXTURE_DIR
#error "NUCLEUS_GOLDEN_FIXTURE_DIR must be defined by the build"
#endif

namespace {

// A parsed case descriptor: the ordered document_paths stack, an optional strain
// selection, and whether this case is a negative (designed-to-diverge) case.
struct fixture_case
{
    std::vector<std::string> inputs;
    std::optional<std::string> selection;
    bool expect_divergence = false;
};

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Parses a case.txt: `input=<file>` lines (order preserved -> the precedence
// stack), an optional `selection=<value>` line, and an optional
// `expect_divergence=true` line. Blank lines and `#` comments are ignored.
fixture_case parse_case(const std::string &text)
{
    fixture_case parsed;
    std::istringstream in(text);
    std::string line;
    while(std::getline(in, line))
    {
        // Trim trailing carriage return (so Windows-authored fixtures parse).
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        if(line.empty() || line.front() == '#')
            continue;
        const auto eq = line.find('=');
        if(eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if(key == "input")
            parsed.inputs.push_back(value);
        else if(key == "selection")
            parsed.selection = value;
        else if(key == "expect_divergence")
            parsed.expect_divergence = (value == "true");
    }
    return parsed;
}

}

TEST_CASE("inheritance golden fixtures match nucleus-derived resolution", "[golden]")
{
    const std::filesystem::path fixture_root(NUCLEUS_GOLDEN_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(fixture_root));

    // Discover case directories deterministically (sorted) so the run is stable.
    std::map<std::string, std::filesystem::path> cases;
    for(const auto &entry : std::filesystem::directory_iterator(fixture_root))
    {
        if(!entry.is_directory())
            continue;
        const std::filesystem::path case_txt = entry.path() / "case.txt";
        if(std::filesystem::exists(case_txt))
            cases.emplace(entry.path().filename().string(), entry.path());
    }

    REQUIRE_FALSE(cases.empty());

    for(const auto &[name, dir] : cases)
    {
        DYNAMIC_SECTION("case: " << name)
        {
            const fixture_case spec = parse_case(read_file(dir / "case.txt"));
            REQUIRE_FALSE(spec.inputs.empty());

            nucleus::configuration_space_builder builder;
            nucleus::golden::declare_schema(builder);
            const nucleus::configuration_space space = builder.build();

            nucleus::load_options opts;
            opts.document_paths = spec.inputs;
            opts.make_document = nucleus::golden::file_factory(dir.string());
            opts.selection = spec.selection;

            nucleus::load_result loaded = nucleus::load(space, nucleus::source_stack{}, opts);
            INFO("load error (if any): " << (loaded ? std::string("<none>") : nucleus::to_string(loaded.error())));
            REQUIRE(loaded);

            const std::string actual = nucleus::golden::serialize(loaded.value());
            const std::string expected = read_file(dir / "expected.txt");
            const std::optional<std::string> mismatch =
                nucleus::golden::diff(expected, actual);

            if(spec.expect_divergence)
            {
                // Negative case: the committed golden is deliberately wrong, so the
                // diff routine MUST report a mismatch -- this proves the harness has
                // seen a real diff, not only matching runs.
                INFO("actual serialization:\n" << actual);
                REQUIRE(mismatch.has_value());
            }
            else
            {
                INFO("mismatch: " << mismatch.value_or("<none>"));
                INFO("actual serialization:\n" << actual);
                REQUIRE_FALSE(mismatch.has_value());
            }
        }
    }
}
