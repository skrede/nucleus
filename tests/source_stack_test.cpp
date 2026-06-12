#include "nucleus/capability.h"

#include "nucleus/config_source/source_concept.h"
#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/source_stack.h"
#include "nucleus/config_source/config_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

// Each labelled stub emits a unique sentinel key so insertion-order dispatch is verifiable.
struct source_alpha
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::ordering};
    }

    [[nodiscard]] nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        batch.entries.push_back(
            nucleus::make_entry("sentinel", nucleus::value::owned("alpha"), capabilities()));
        return batch;
    }
};

struct source_beta
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::nesting};
    }

    [[nodiscard]] nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        batch.entries.push_back(
            nucleus::make_entry("sentinel", nucleus::value::owned("beta"), capabilities()));
        return batch;
    }
};

struct source_gamma
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::nesting, nucleus::capability::typed_scalars};
    }

    [[nodiscard]] nucleus::inherit_declaration inheritance() const
    {
        return {nucleus::inherit_declaration::kind::parent_path, "parent.xml"};
    }

    [[nodiscard]] nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        batch.entries.push_back(
            nucleus::make_entry("sentinel", nucleus::value::owned("gamma"), capabilities()));
        return batch;
    }
};

// Contest stubs: both emit the same key "shared/value" with different payloads.
// lower_source is listed first (lower index = lower rank); higher_source is listed
// last (higher index = higher rank = last-listed-wins).
struct lower_source
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const { return {}; }

    [[nodiscard]] nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        batch.entries.push_back(
            nucleus::make_entry("shared/value", nucleus::value::owned("from-lower"), capabilities()));
        return batch;
    }
};

struct higher_source
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const { return {}; }

    [[nodiscard]] nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        batch.entries.push_back(
            nucleus::make_entry("shared/value", nucleus::value::owned("from-higher"), capabilities()));
        return batch;
    }
};

}

TEST_CASE("source_stack default-constructed with no sources is empty", "[source_stack]")
{
    // Zero-argument variadic ctor: sizeof...(Ss)==0 is valid C++20.
    nucleus::source_stack empty{};
    REQUIRE(empty.size() == 0);
    REQUIRE(empty.empty());
}

TEST_CASE("source_stack with three sources reports size 3 and is not empty", "[source_stack]")
{
    nucleus::source_stack stack{source_alpha{}, source_beta{}, source_gamma{}};

    REQUIRE(stack.size() == 3);
    REQUIRE_FALSE(stack.empty());
}

TEST_CASE("source_stack layers()[0] dispatches to the first-listed source (alpha)", "[source_stack]")
{
    nucleus::source_stack stack{source_alpha{}, source_beta{}, source_gamma{}};

    auto layers = stack.layers();
    REQUIRE(layers.size() == 3);

    auto result = layers[0].pull();
    REQUIRE(result);
    REQUIRE(result.value().entries.size() == 1);
    REQUIRE(result.value().entries[0].value.text() == "alpha");
}

TEST_CASE("source_stack layers()[2] dispatches to the last-listed source (gamma)", "[source_stack]")
{
    nucleus::source_stack stack{source_alpha{}, source_beta{}, source_gamma{}};

    auto layers = stack.layers();

    auto result = layers[2].pull();
    REQUIRE(result);
    REQUIRE(result.value().entries[0].value.text() == "gamma");
}

TEST_CASE("source_stack index equals insertion order: layers()[1] dispatches to beta", "[source_stack]")
{
    nucleus::source_stack stack{source_alpha{}, source_beta{}, source_gamma{}};

    auto layers = stack.layers();
    auto caps = layers[1].capabilities();
    // beta declares nesting but not typed_scalars; alpha declares ordering.
    REQUIRE(caps.supports(nucleus::capability::nesting));
    REQUIRE_FALSE(caps.supports(nucleus::capability::typed_scalars));
    REQUIRE_FALSE(caps.supports(nucleus::capability::ordering));

    auto result = layers[1].pull();
    REQUIRE(result);
    REQUIRE(result.value().entries[0].value.text() == "beta");
}

TEST_CASE("source_stack: a flat stub and a projecting+inheriting stub coexist at adjacent indices", "[source_stack]")
{
    // flat_stub = source_alpha (no optional ops), gamma = projecting+inheriting.
    nucleus::source_stack stack{source_alpha{}, source_gamma{}};

    auto layers = stack.layers();

    // Flat slot: inheritance defaults to inherit_default.
    auto decl0 = layers[0].inheritance();
    REQUIRE(decl0.which == nucleus::inherit_declaration::kind::inherit_default);

    // Full slot: inheritance forwards to the wrapped struct.
    auto decl1 = layers[1].inheritance();
    REQUIRE(decl1.which == nucleus::inherit_declaration::kind::parent_path);
    REQUIRE(decl1.path == "parent.xml");
}

TEST_CASE("source_stack index-as-precedence: last-listed source wins a same-key contest", "[source_stack]")
{
    // lower_source at index 0 (rank 0), higher_source at index 1 (rank 1 = higher precedence).
    nucleus::source_stack stack{lower_source{}, higher_source{}};

    auto layers = stack.layers();
    REQUIRE(layers.size() == 2);

    // Pull both handles and read the shared key from each.
    auto result_low  = layers[0].pull();
    auto result_high = layers[1].pull();

    REQUIRE(result_low);
    REQUIRE(result_high);

    // The contest key is "shared/value" in both results.
    REQUIRE(result_low.value().entries[0].path == "shared/value");
    REQUIRE(result_high.value().entries[0].path == "shared/value");

    // The LAST-listed (higher index = higher rank) source's value wins.
    // In a fold that assigns rank by ascending index the last entry overwrites.
    REQUIRE(result_low.value().entries[0].value.text()  == "from-lower");
    REQUIRE(result_high.value().entries[0].value.text() == "from-higher");

    // Precedence assertion: the last-listed handle (index 1) holds the winning value.
    const std::size_t last_index = layers.size() - 1;
    auto winning_result = layers[last_index].pull();
    REQUIRE(winning_result);
    REQUIRE(winning_result.value().entries[0].value.text() == "from-higher");
}

TEST_CASE("source_stack push_back appends a pre-erased handle and increments size", "[source_stack]")
{
    nucleus::source_stack stack{source_alpha{}};
    REQUIRE(stack.size() == 1);

    stack.push_back(nucleus::source_handle{source_beta{}});
    REQUIRE(stack.size() == 2);

    auto layers = stack.layers();
    auto result = layers[1].pull();
    REQUIRE(result);
    REQUIRE(result.value().entries[0].value.text() == "beta");
}
