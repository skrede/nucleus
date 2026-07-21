#include "nucleus/error.h"
#include "nucleus/config.h"

#include "nucleus/keyspace/provenance.h"

#include "nucleus/env/env_emitter.h"
#include "nucleus/argv/argv_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <numeric>
#include <sstream>

// The flat (argv/env) emitters share one helper (emit_flat_document): repeated
// instances must round-trip in numeric ordinal order, and a value carrying a
// line break must be refused before anything is written -- otherwise a
// lower-precedence value could forge an extra well-formed flag/var line.

namespace {

nucleus::config make_repeated_config(int count)
{
    std::map<std::string, std::string> values;
    for(int i = 0; i < count; ++i)
        values.emplace("cluster/node[" + std::to_string(i) + "]/port",
                       std::to_string(i));
    return nucleus::config(std::move(values), nucleus::provenance{});
}

std::vector<int> emitted_ordinal_values(const std::string &text)
{
    std::vector<int> out;
    std::istringstream lines(text);
    std::string line;
    while(std::getline(lines, line))
    {
        const auto eq = line.find('=');
        REQUIRE(eq != std::string::npos);
        REQUIRE(line.substr(0, eq) == "cluster/node/port");
        out.push_back(std::stoi(line.substr(eq + 1)));
    }
    return out;
}

}

TEST_CASE("flat emit orders repeated instances by numeric ordinal at N >= 11",
          "[flat][emit][env][ordering]")
{
    // std::map stores keys lexicographically (node[10] before node[2]); the
    // emitter must re-order them so the instances round-trip in ordinal order.
    const nucleus::config cfg = make_repeated_config(12);

    std::ostringstream out;
    REQUIRE(nucleus::env::emit_document(cfg, out));

    std::vector<int> expected(12);
    std::iota(expected.begin(), expected.end(), 0);
    REQUIRE(emitted_ordinal_values(out.str()) == expected);
}

TEST_CASE("flat emit rejects an embedded newline as malformed_source, writing nothing",
          "[flat][emit][env][injection]")
{
    std::map<std::string, std::string> values{
        {"server/host", std::string("localhost\n--server-admin=true")}};
    const nucleus::config cfg(std::move(values), nucleus::provenance{});

    std::ostringstream out;
    const auto result = nucleus::env::emit_document(cfg, out);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(result.error().message.find("server/host") != std::string::npos);
    REQUIRE(out.str().empty());
}

TEST_CASE("flat emit rejects an embedded carriage return as malformed_source",
          "[flat][emit][env][injection]")
{
    std::map<std::string, std::string> values{
        {"server/host", std::string("localhost\r--server-admin=true")}};
    const nucleus::config cfg(std::move(values), nucleus::provenance{});

    std::ostringstream out;
    const auto result = nucleus::env::emit_document(cfg, out);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(result.error().message.find("server/host") != std::string::npos);
    REQUIRE(out.str().empty());
}

TEST_CASE("argv emit inherits the shared newline-rejection fix",
          "[flat][emit][argv][injection]")
{
    std::map<std::string, std::string> values{
        {"server/host", std::string("localhost\n--server-admin=true")}};
    const nucleus::config cfg(std::move(values), nucleus::provenance{});

    std::ostringstream out;
    const auto result = nucleus::argv::emit_document(cfg, out);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(out.str().empty());
}

TEST_CASE("flat emit rejects an embedded newline in a KEY, writing nothing",
          "[flat][emit][env][injection]")
{
    // A key -- not just a value -- carrying a line break would forge a second
    // well-formed assignment line on the flat grammar. The pre-scan must reject
    // it before anything reaches the stream.
    std::map<std::string, std::string> values{
        {std::string("server/host\n--server-admin"), std::string("true")}};
    const nucleus::config cfg(std::move(values), nucleus::provenance{});

    std::ostringstream out;
    const auto result = nucleus::env::emit_document(cfg, out);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(result.error().message.find("key") != std::string::npos);
    REQUIRE(out.str().empty());

    std::ostringstream args_out;
    const auto args_result = nucleus::argv::emit_document(cfg, args_out);
    REQUIRE_FALSE(args_result);
    REQUIRE(args_result.error().code == nucleus::errc::malformed_source);
    REQUIRE(args_out.str().empty());
}

TEST_CASE("flat emit rejects an embedded carriage return in a KEY",
          "[flat][emit][env][injection]")
{
    std::map<std::string, std::string> values{
        {std::string("server/host\r--server-admin"), std::string("true")}};
    const nucleus::config cfg(std::move(values), nucleus::provenance{});

    std::ostringstream out;
    const auto result = nucleus::env::emit_document(cfg, out);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == nucleus::errc::malformed_source);
    REQUIRE(out.str().empty());
}
