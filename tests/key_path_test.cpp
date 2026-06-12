#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using nucleus::key_path;

TEST_CASE("key_path parses a `/`-separated FQN into segments", "[key_path]")
{
    auto path = key_path::parse("a/b/c");
    REQUIRE(path);
    REQUIRE(path.value().size() == 3);
    REQUIRE(path.value().front() == "a");
    REQUIRE(path.value().leaf() == "c");
    REQUIRE(path.value().str() == "a/b/c");
}

TEST_CASE("key_path round-trips through its canonical string", "[key_path]")
{
    auto path = key_path::parse("plexus/udp/auth_mode");
    REQUIRE(path);
    auto again = key_path::parse(path.value().str());
    REQUIRE(again);
    REQUIRE(again.value() == path.value());
}

TEST_CASE("key_path rejects empty and malformed paths", "[key_path]")
{
    REQUIRE_FALSE(key_path::parse(""));
    REQUIRE_FALSE(key_path::parse("a//b"));
    REQUIRE_FALSE(key_path::parse("/a"));
    REQUIRE_FALSE(key_path::parse("a/"));
}

TEST_CASE("key_path parent and child navigate the hierarchy", "[key_path]")
{
    auto path = key_path::parse("a/b/c").value();
    REQUIRE(path.parent().str() == "a/b");
    REQUIRE(path.parent().parent().str() == "a");
    REQUIRE(path.parent().parent().parent().empty());
    REQUIRE(path.parent().child("x").str() == "a/b/x");
}

TEST_CASE("key_path from validated segments", "[key_path]")
{
    key_path path(std::vector<std::string>{"a", "b"});
    REQUIRE(path.str() == "a/b");
    REQUIRE(path.size() == 2);
}

TEST_CASE("key_path join concatenates segment-wise", "[key_path]")
{
    const key_path base = key_path::parse("a/b").value();
    const key_path tail = key_path::parse("c/d").value();
    REQUIRE(base.join(tail).str() == "a/b/c/d");
    REQUIRE(key_path{}.join(tail) == tail);
    REQUIRE(base.join(key_path{}) == base);
}

TEST_CASE("key_path starts_with matches leading segments, not text prefixes", "[key_path]")
{
    const key_path path = key_path::parse("server/host_name").value();
    REQUIRE(path.starts_with(key_path::parse("server").value()));
    REQUIRE(path.starts_with(path));
    REQUIRE(path.starts_with(key_path{}));
    // `host` is a TEXT prefix of the `host_name` segment, not a segment match.
    REQUIRE_FALSE(key_path::parse("server/host_name/x").value()
                      .starts_with(key_path::parse("server/host").value()));
    REQUIRE_FALSE(key_path::parse("server").value().starts_with(path));
}

TEST_CASE("key_path relative_to strips a leading prefix", "[key_path]")
{
    const key_path path = key_path::parse("plugin/alpha/udp/auth_mode").value();
    const key_path anchor = key_path::parse("plugin/alpha").value();
    REQUIRE(path.relative_to(anchor).str() == "udp/auth_mode");
    REQUIRE(path.relative_to(key_path{}) == path);
    REQUIRE(path.relative_to(path).empty());
}

TEST_CASE("indexed segment helpers", "[key_path][indexed]")
{
    SECTION("is_indexed_segment accepts well-formed bracket notation")
    {
        REQUIRE(key_path::is_indexed_segment("node[0]"));
        REQUIRE(key_path::is_indexed_segment("node[12]"));
        REQUIRE(key_path::is_indexed_segment("node[42]"));
    }

    SECTION("is_indexed_segment rejects ill-formed variants")
    {
        REQUIRE_FALSE(key_path::is_indexed_segment("node"));
        REQUIRE_FALSE(key_path::is_indexed_segment("node[]"));   // empty digits
        REQUIRE_FALSE(key_path::is_indexed_segment("[0]"));      // empty base name
        REQUIRE_FALSE(key_path::is_indexed_segment("node[0x1]")); // non-decimal digit
    }

    SECTION("base_name extracts the part before the bracket")
    {
        REQUIRE(key_path::base_name("node[0]") == "node");
        REQUIRE(key_path::base_name("node") == "node");  // non-indexed: unchanged
    }

    SECTION("ordinal_of parses the decimal index")
    {
        REQUIRE(key_path::ordinal_of("node[0]") == 0);
        REQUIRE(key_path::ordinal_of("node[7]") == 7);
        REQUIRE(key_path::ordinal_of("node[42]") == 42);
    }

    SECTION("parse accepts indexed segments as a whole segment token")
    {
        auto result = key_path::parse("cluster/node[0]/port");
        REQUIRE(result);
        REQUIRE(result.value().segments().size() == 3);
        REQUIRE(result.value().segments()[0] == "cluster");
        REQUIRE(result.value().segments()[1] == "node[0]");
        REQUIRE(result.value().segments()[2] == "port");
        REQUIRE(result.value().str() == "cluster/node[0]/port");
    }

    SECTION("parse rejects malformed bracket notation")
    {
        auto bad_alpha = key_path::parse("cluster/node[bad]/port");
        REQUIRE_FALSE(bad_alpha);
        REQUIRE(bad_alpha.error().find("malformed indexed segment") != std::string::npos);

        auto empty_idx = key_path::parse("cluster/node[]/port");
        REQUIRE_FALSE(empty_idx);
        REQUIRE(empty_idx.error().find("malformed indexed segment") != std::string::npos);
    }

    SECTION("is_indexed_segment rejects digit runs longer than 18 chars -- overflow guard")
    {
        // 19-digit ordinal -- exceeds safe 64-bit range.
        REQUIRE_FALSE(key_path::is_indexed_segment("node[9999999999999999999]"));
        // 20-digit ordinal.
        REQUIRE_FALSE(key_path::is_indexed_segment("node[99999999999999999999]"));
        // 18-digit ordinal is still accepted (max safe range).
        REQUIRE(key_path::is_indexed_segment("node[999999999999999999]"));
    }

    SECTION("parse treats 19-digit ordinal as a malformed indexed segment")
    {
        // is_indexed_segment rejects it, so parse() surfaces the malformed-segment error.
        auto result = key_path::parse("cluster/node[9999999999999999999]/port");
        REQUIRE_FALSE(result);
        REQUIRE(result.error().find("malformed indexed segment") != std::string::npos);
    }
}

TEST_CASE("is_indexed_segment rejects leading-zero ordinals", "[key_path][indexed][IN01]")
{
    // A lone "0" is the valid zero-ordinal; leading zero in a multi-digit sequence is rejected.
    REQUIRE(key_path::is_indexed_segment("node[0]"));
    REQUIRE_FALSE(key_path::is_indexed_segment("node[01]"));
    REQUIRE_FALSE(key_path::is_indexed_segment("node[00]"));
    REQUIRE_FALSE(key_path::is_indexed_segment("node[007]"));

    // parse() rejects the malformed segment.
    auto result = key_path::parse("cluster/node[01]/port");
    REQUIRE_FALSE(result);
    REQUIRE(result.error().find("malformed indexed segment") != std::string::npos);
}
