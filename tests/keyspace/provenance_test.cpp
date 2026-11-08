#include "nucleus/keyspace/provenance.h"

#include "nucleus/identity.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstddef>
#include <optional>

using nucleus::origin;
using nucleus::provenance;

namespace {

origin chain_write(std::size_t layer) { return origin{1, "chain", nucleus::owner_token{}, layer}; }

origin flat_write() { return origin{2, "flat", nucleus::owner_token{}, std::nullopt}; }

}

TEST_CASE("the last chain writer replaces an earlier one and survives a flat overwrite", "[keyspace][provenance]")
{
    provenance where;
    where.record("a/b", chain_write(0));
    where.record("a/b", flat_write());
    where.record("a/b", chain_write(2));
    where.record("a/b", flat_write());

    REQUIRE(where.first_inheritance_layer_of("a/b") != nullptr);
    REQUIRE(where.last_inheritance_layer_of("a/b") != nullptr);
    CHECK(*where.first_inheritance_layer_of("a/b") == 0);
    CHECK(*where.last_inheritance_layer_of("a/b") == 2);
    CHECK(where.of("a/b")->layer == "flat");
}

TEST_CASE("forgetting a key drops both chain writers", "[keyspace][provenance]")
{
    provenance where;
    where.record("a/b", chain_write(0));
    where.record("a/b", chain_write(2));
    where.forget("a/b");

    CHECK(where.first_inheritance_layer_of("a/b") == nullptr);
    CHECK(where.last_inheritance_layer_of("a/b") == nullptr);
    CHECK(where.of("a/b") == nullptr);
}

TEST_CASE("a key only flat sources wrote has no chain writer", "[keyspace][provenance]")
{
    provenance where;
    where.record("a/b", flat_write());

    CHECK(where.first_inheritance_layer_of("a/b") == nullptr);
    CHECK(where.last_inheritance_layer_of("a/b") == nullptr);
}
