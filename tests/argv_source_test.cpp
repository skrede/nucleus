#include "nucleus/log_sink.h"

#include "nucleus/configuration_source/source_handle.h"
#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/configuration_source/argv/argv_source.h"
#include "nucleus/configuration_source/argv/cli_surface.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>
#include <string_view>

using nucleus::key_path;
using nucleus::argv_source;
using nucleus::normalize_arg;

namespace {

// Collects the warn-level messages a source emits, to observe lenient behavior.
class capturing_sink final : public nucleus::log_sink
{
public:
    void log(nucleus::log_level level, std::string_view message) override
    {
        if(level == nucleus::log_level::warn)
            warnings.emplace_back(message);
    }

    std::vector<std::string> warnings;
};

}

TEST_CASE("normalize_arg maps `-` to the keyspace separator", "[argv]")
{
    auto kv = normalize_arg("--plexus-udp-auth_mode=auth");
    REQUIRE(kv);
    REQUIRE(kv.value().key.str() == "plexus/udp/auth_mode");
    REQUIRE(kv.value().value == "auth");
}

TEST_CASE("normalize_arg treats a bare flag as a truthy presence", "[argv]")
{
    auto kv = normalize_arg("--node-anonymous");
    REQUIRE(kv);
    REQUIRE(kv.value().key.str() == "node/anonymous");
    REQUIRE(kv.value().value == "true");
}

TEST_CASE("normalize_arg splits on the first `=` only", "[argv]")
{
    auto kv = normalize_arg("--token-url=https://h/p?a=b&c=d");
    REQUIRE(kv);
    REQUIRE(kv.value().key.str() == "token/url");
    REQUIRE(kv.value().value == "https://h/p?a=b&c=d");
}

TEST_CASE("normalize_arg rejects non-`--` tokens", "[argv]")
{
    REQUIRE_FALSE(normalize_arg("plexus-udp=x"));
    REQUIRE_FALSE(normalize_arg("--"));
    REQUIRE_FALSE(normalize_arg("--=x"));
}

TEST_CASE("the `-` <-> `/` bijection is invertible", "[argv]")
{
    auto kv = normalize_arg("--plexus-udp-auth_mode=auth").value();
    REQUIRE(nucleus::flag_of(kv.key) == "--plexus-udp-auth_mode");
}

TEST_CASE("argv_source emits keyspace entries through the source seam", "[argv]")
{
    argv_source src(std::vector<std::string>{
        "--plexus-udp-auth_mode=auth", "--node-name=alpha"});

    nucleus::source_handle seam{std::move(src)}; // pulled through the erased seam
    auto batch = seam.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries.size() == 2);
    REQUIRE(batch.value().entries[0].path == "plexus/udp/auth_mode");
    REQUIRE(batch.value().entries[0].value.text() == "auth");
    REQUIRE(batch.value().entries[1].path == "node/name");
}

TEST_CASE("schema validation is a separate step after mapping (strict)", "[argv]")
{
    std::set<std::string> declared{"plexus/udp/auth_mode"};
    argv_source src(std::vector<std::string>{"--plexus-udp-bogus=x"});
    src.recognize_with([&](const key_path &p)
                       { return declared.count(p.str()) != 0; });

    auto batch = src.pull();
    REQUIRE_FALSE(batch); // strict by default: unknown path is an error
    REQUIRE(batch.error().find("undeclared key") != std::string::npos);
}

TEST_CASE("lenient mode stores unknown flags as strings with a warning", "[argv]")
{
    std::set<std::string> declared{"plexus/udp/auth_mode"};
    capturing_sink sink;
    argv_source src(std::vector<std::string>{"--plexus-udp-bogus=x"});
    src.recognize_with([&](const key_path &p)
                       { return declared.count(p.str()) != 0; })
        .policy(nucleus::unknown_key_policy::lenient)
        .log_to(sink);

    auto batch = src.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries.size() == 1);
    REQUIRE(batch.value().entries[0].value.text() == "x");
    REQUIRE_FALSE(sink.warnings.empty());
}

TEST_CASE("a recognized flag passes strict validation", "[argv]")
{
    std::set<std::string> declared{"plexus/udp/auth_mode"};
    argv_source src(std::vector<std::string>{"--plexus-udp-auth_mode=auth"});
    src.recognize_with([&](const key_path &p)
                       { return declared.count(p.str()) != 0; });

    auto batch = src.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries.size() == 1);
}
