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
