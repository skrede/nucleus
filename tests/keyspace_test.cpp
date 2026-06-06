#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>

using nucleus::value;
using nucleus::keyspace;
using nucleus::key_path;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

bool contains_segment(const std::vector<std::string> &v, const std::string &s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

}

TEST_CASE("keyspace stores and finds values by hierarchical path", "[keyspace]")
{
    keyspace ks;
    ks.set(path_of("a/b/c"), value::owned("v"));

    REQUIRE(ks.size() == 1);
    REQUIRE(ks.contains(path_of("a/b/c")));
    REQUIRE(ks.find(path_of("a/b/c")) != nullptr);
    REQUIRE(ks.find(path_of("a/b/c"))->text() == "v");
    REQUIRE(ks.find(path_of("a/b")) == nullptr);
}

TEST_CASE("keyspace set is last-write-wins at a leaf", "[keyspace]")
{
    keyspace ks;
    ks.set(path_of("x/y"), value::owned("first"));
    ks.set(path_of("x/y"), value::owned("second"));
    REQUIRE(ks.size() == 1);
    REQUIRE(ks.find(path_of("x/y"))->text() == "second");
}

TEST_CASE("keyspace surfaces intermediate nodes structurally", "[keyspace]")
{
    keyspace ks;
    ks.set(path_of("a/b/c"), value::owned("1"));
    ks.set(path_of("a/b/d/e"), value::owned("2"));
    ks.set(path_of("a/f"), value::owned("3"));

    REQUIRE(ks.has_node(path_of("a")));
    REQUIRE(ks.has_node(path_of("a/b")));
    REQUIRE_FALSE(ks.has_node(path_of("a/z")));

    auto top = ks.children_of(key_path{});
    REQUIRE(contains_segment(top, "a"));

    auto under_a = ks.children_of(path_of("a"));
    REQUIRE(contains_segment(under_a, "b"));
    REQUIRE(contains_segment(under_a, "f"));

    auto under_ab = ks.children_of(path_of("a/b"));
    REQUIRE(contains_segment(under_ab, "c"));
    REQUIRE(contains_segment(under_ab, "d"));
    REQUIRE(under_ab.size() == 2);
}

TEST_CASE("keyspace paths enumerates every leaf", "[keyspace]")
{
    keyspace ks;
    ks.set(path_of("a/b"), value::owned("1"));
    ks.set(path_of("c"), value::owned("2"));
    auto paths = ks.paths();
    REQUIRE(paths.size() == 2);
}
