#include "nucleus/identity.h"

#include "nucleus/diagnostics/key_suggester.h"
#include "nucleus/diagnostics/conflict_report.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Unknown-key suggestion: a typo'd key should surface its nearest declared
// neighbor first.

TEST_CASE("an unknown key suggests the nearest declared key", "[diagnostics][suggest]")
{
    std::vector<std::string> known{
        "logging/level", "logging/file", "server/host", "server/port"};

    auto suggestions = nucleus::suggest_keys("logging/levle", known, 1);
    REQUIRE(suggestions.size() == 1);
    REQUIRE(suggestions.front() == "logging/level");
}

TEST_CASE("a same-class substitution ranks ahead of a cross-class one", "[diagnostics][suggest]")
{
    // "levet" swaps a lowercase letter for a lowercase letter (same class, cost
    // 0.5); "leve1" swaps a letter for a digit (cross class, cost 1.0). The
    // same-class typo should rank first despite both being one edit away.
    std::vector<std::string> known{"leve1", "levet"};
    auto suggestions = nucleus::suggest_keys("level", known, 2);
    REQUIRE(suggestions.size() == 2);
    REQUIRE(suggestions.front() == "levet");
}

TEST_CASE("suggestions are bounded and deterministic on ties", "[diagnostics][suggest]")
{
    std::vector<std::string> known{"alpha", "alphb", "alphc"};
    auto suggestions = nucleus::suggest_keys("alph", known, 2);
    REQUIRE(suggestions.size() == 2);
    // Equal distance -> lexicographic order, deterministically.
    REQUIRE(suggestions[0] == "alpha");
    REQUIRE(suggestions[1] == "alphb");
}

TEST_CASE("no candidates yields no suggestions", "[diagnostics][suggest]")
{
    std::vector<std::string> none;
    REQUIRE(nucleus::suggest_keys("anything", none).empty());
}

// Conflict reporting: name the owning tokens and locations; do NOT adjudicate.

TEST_CASE("a key-path conflict names every claimant without choosing a winner", "[diagnostics][conflict]")
{
    nucleus::owner_token plugin_a(std::string("plugin.a"));
    nucleus::owner_token plugin_b(std::string("plugin.b"));

    nucleus::conflict_report report("server/port");
    report.add({"plugin.a registered at base.conf:12", plugin_a})
          .add({"plugin.b registered at overlay.conf:3", plugin_b});

    REQUIRE(report.key_path() == "server/port");
    REQUIRE(report.size() == 2);

    const std::string text = report.describe();
    // Both claimants' locations are surfaced.
    REQUIRE(text.find("server/port") != std::string::npos);
    REQUIRE(text.find("base.conf:12") != std::string::npos);
    REQUIRE(text.find("overlay.conf:3") != std::string::npos);
    // The report explicitly refuses to adjudicate.
    REQUIRE(text.find("no winner") != std::string::npos);
}

TEST_CASE("claimant owner tokens are retained for host adjudication", "[diagnostics][conflict]")
{
    nucleus::owner_token plugin_a(std::string("plugin.a"));
    nucleus::owner_token plugin_b(std::string("plugin.b"));

    nucleus::conflict_report report("k");
    report.add({"a", plugin_a}).add({"b", plugin_b});

    // The tokens travel with the report so the host can compare identities -- the
    // engine never interprets them itself.
    REQUIRE(report.claimants().front().owner == plugin_a);
    REQUIRE_FALSE(report.claimants().front().owner == plugin_b);
}
