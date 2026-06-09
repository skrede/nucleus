#include "nucleus/capability.h"

#include "nucleus/configuration_source/source_concept.h"
#include "nucleus/configuration_source/source_handle.h"
#include "nucleus/configuration_source/configuration_source.h"
#include "nucleus/configuration_source/discovery.h"
#include "nucleus/configuration_source/extension_registry.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <random>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace {

// A trivial source that reports which file it was built from, so a test can
// assert discovery wired the right factory to the right path.
// Plain struct satisfying the source concept by duck typing.
struct labelled_source
{
    explicit labelled_source(std::string label) : m_label(std::move(label)) {}

    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {};
    }

    [[nodiscard]] nucleus::configuration_source_result pull()
    {
        nucleus::configuration_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "where", nucleus::value::owned(m_label), capabilities()));
        return batch;
    }

    std::string m_label;
};

static_assert(nucleus::configuration_source<labelled_source>,
              "labelled_source must satisfy the source concept");

// A factory that tags the source it builds with the format name, so the test can
// see which parser claimed the discovered file.
nucleus::parser_factory tagging_factory(std::string format)
{
    return [format](const std::string &path) -> nucleus::source_handle {
        return nucleus::source_handle(labelled_source(format + ":" + path));
    };
}

// A process-unique temp directory base, so concurrent test runs do not collide.
std::filesystem::path unique_temp_dir(const std::string &prefix)
{
    std::random_device rng;
    return std::filesystem::temp_directory_path() /
           (prefix + std::to_string(rng()) + "_" + std::to_string(rng()));
}

// Writes an empty file at `path`, creating parent directories as needed.
void touch(const std::filesystem::path &path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << "";
}

}

TEST_CASE("each extension resolves to exactly one parser", "[extension]")
{
    nucleus::extension_registry registry;

    auto first = registry.claim({"xml"}, tagging_factory("xml"));
    REQUIRE(first);

    // A second parser claiming the same extension is a registration-time error.
    auto clash = registry.claim({"xml"}, tagging_factory("other"));
    REQUIRE_FALSE(clash);
    REQUIRE(clash.error().find(".xml") != std::string::npos);

    // The clash committed nothing: the original parser still owns the extension.
    REQUIRE(registry.size() == 1);
    auto built = registry.open("config.xml");
    REQUIRE(built.has_value());
    auto pulled = built->pull();
    REQUIRE(pulled);
    REQUIRE(pulled.value().entries.at(0).value.text() == "xml:config.xml");
}

TEST_CASE("a leading dot and a bare extension normalize to the same claim", "[extension]")
{
    nucleus::extension_registry registry;

    REQUIRE(registry.claim({".ini"}, tagging_factory("ini")));
    // ".ini" already claimed; "ini" must normalize to the same key and clash.
    REQUIRE_FALSE(registry.claim({"ini"}, tagging_factory("ini-again")));
    REQUIRE(registry.size() == 1);
}

TEST_CASE("one parser may claim several extensions", "[extension]")
{
    nucleus::extension_registry registry;

    REQUIRE(registry.claim({"xml", "config"}, tagging_factory("xml")));
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.claims(".xml"));
    REQUIRE(registry.claims("config"));

    // Both extensions resolve to the same parser.
    REQUIRE(registry.open("app.xml")->pull().value().entries.at(0).value.text()
            == "xml:app.xml");
    REQUIRE(registry.open("app.config")->pull().value().entries.at(0).value.text()
            == "xml:app.config");
}

TEST_CASE("an extension claimed twice in one call is rejected atomically", "[extension]")
{
    nucleus::extension_registry registry;

    // The same extension appears twice in one claim(). The map would silently
    // no-op the second emplace, so the registry must reject it as a
    // registration-time error and commit nothing.
    auto dup = registry.claim({".cfg", ".cfg"}, tagging_factory("cfg"));
    REQUIRE_FALSE(dup);
    REQUIRE(dup.error().find(".cfg") != std::string::npos);
    REQUIRE(registry.size() == 0);
    REQUIRE_FALSE(registry.claims(".cfg"));

    // The leading-dot/bare normalization also collapses to a duplicate.
    auto dup_norm = registry.claim({"cfg", ".cfg"}, tagging_factory("cfg"));
    REQUIRE_FALSE(dup_norm);
    REQUIRE(registry.size() == 0);
}

TEST_CASE("discovery finds host-supplied base name across host-supplied paths", "[discovery]")
{
    namespace fs = std::filesystem;
    fs::path root = unique_temp_dir("nucleus_discovery_");
    fs::remove_all(root);

    fs::path etc = root / "etc";
    fs::path home = root / "home";

    nucleus::extension_registry registry;
    REQUIRE(registry.claim({"xml"}, tagging_factory("xml")));
    REQUIRE(registry.claim({"ini"}, tagging_factory("ini")));

    // The host's policy: base name "settings", and a precedence order of
    // directories. No filename is baked into core.
    const std::string base = "settings";
    touch(etc / (base + ".ini"));
    touch(home / (base + ".xml"));
    touch(home / "unrelated.xml");          // wrong base name -- must be ignored.

    std::vector<fs::path> search_paths{home, etc};

    auto hits = nucleus::discovery::find(base, search_paths, registry);

    // Two matching files exist (home/settings.xml and etc/settings.ini); the
    // unrelated file is excluded.
    REQUIRE(hits.size() == 2);

    auto has = [&](const std::string &needle) {
        return std::any_of(hits.begin(), hits.end(),
                           [&](const nucleus::discovered_source &d) {
                               return d.path.find(needle) != std::string::npos;
                           });
    };
    REQUIRE(has("settings.xml"));
    REQUIRE(has("settings.ini"));

    // Host precedence is preserved: home (first search dir) outranks etc.
    // Compare in the engine's canonical path text (generic, forward-slash):
    // native string() would carry backslashes on Windows and never match.
    REQUIRE(hits.front().path.find(nucleus::path_to_text(home)) != std::string::npos);

    // open_all builds a live source per hit through the registry's factories.
    auto sources = nucleus::discovery::open_all(base, search_paths, registry);
    REQUIRE(sources.size() == 2);
    for(auto &src : sources)
        REQUIRE(src.pull());

    fs::remove_all(root);
}

TEST_CASE("discovery is a no-op without a registered extension or a matching file", "[discovery]")
{
    namespace fs = std::filesystem;
    fs::path root = unique_temp_dir("nucleus_discovery_empty_");
    fs::remove_all(root);
    fs::create_directories(root);

    nucleus::extension_registry empty_registry;

    // No claimed extensions -> nothing to look for, even if files exist.
    touch(root / "settings.xml");
    REQUIRE(nucleus::discovery::find("settings", {root}, empty_registry).empty());

    // Claimed extension but no matching base name -> still nothing.
    nucleus::extension_registry registry;
    REQUIRE(registry.claim({"xml"}, tagging_factory("xml")));
    REQUIRE(nucleus::discovery::find("absent", {root}, registry).empty());

    fs::remove_all(root);
}
