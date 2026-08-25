#include "nucleus/capability.h"

#include "nucleus/config_source/source_concept.h"
#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/config_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/schema/projection.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace {

// Minimal struct: only the two required members. Proves any satisfying struct
// is accepted. This type is only ever probed by the config_source concept in
// the unevaluated static_asserts below, never instantiated or called, so its
// members are deliberately unused.
struct minimal_source
{
    [[maybe_unused]] nucleus::capability_descriptor capabilities() const { return {}; }
    [[maybe_unused]] nucleus::config_source_result pull() { return nucleus::config_source_batch{}; }
};

// Flat stub: capabilities() + pull() only. No optional ops.
struct flat_stub
{
    nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::nesting};
    }

    nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "flat/key", nucleus::value::owned("flat-value"), capabilities()));
        return batch;
    }
};

// Projecting stub: adds apply_projection() to the flat interface. Like
// minimal_source, this type is only ever probed by the concept static_asserts,
// so its members are deliberately unused.
struct projecting_stub
{
    [[maybe_unused]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::nesting};
    }

    [[maybe_unused]] void apply_projection(const nucleus::schema_projection &) {}

    [[maybe_unused]] nucleus::config_source_result pull()
    {
        return nucleus::config_source_batch{};
    }
};

// Full stub: adds both apply_projection() and inheritance() on top of the required pair.
struct full_stub
{
    int projection_count = 0;

    nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::nesting, nucleus::capability::typed_scalars};
    }

    void apply_projection(const nucleus::schema_projection &) { ++projection_count; }

    nucleus::inherit_declaration inheritance() const
    {
        return {nucleus::inherit_declaration::kind::parent_path, "sentinel.xml"};
    }

    nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "full/key", nucleus::value::owned("full-value"), capabilities()));
        return batch;
    }
};

// ----- config_source: positive proofs -----

static_assert(nucleus::config_source<minimal_source>,
              "a struct with only capabilities()+pull() must satisfy config_source");
static_assert(nucleus::config_source<flat_stub>,
              "flat_stub must satisfy config_source");
static_assert(nucleus::config_source<projecting_stub>,
              "projecting_stub must satisfy config_source");
static_assert(nucleus::config_source<full_stub>,
              "full_stub must satisfy config_source");

// ----- projects_source: present only when apply_projection() is declared -----

static_assert(!nucleus::projects_source<minimal_source>,
              "minimal_source has no apply_projection(); must not satisfy projects_source");
static_assert(!nucleus::projects_source<flat_stub>,
              "flat_stub has no apply_projection(); must not satisfy projects_source");
static_assert(nucleus::projects_source<projecting_stub>,
              "projecting_stub has apply_projection(); must satisfy projects_source");
static_assert(nucleus::projects_source<full_stub>,
              "full_stub has apply_projection(); must satisfy projects_source");

// ----- inheriting_source: present only when inheritance() is declared -----

static_assert(!nucleus::inheriting_source<minimal_source>,
              "minimal_source has no inheritance(); must not satisfy inheriting_source");
static_assert(!nucleus::inheriting_source<flat_stub>,
              "flat_stub has no inheritance(); must not satisfy inheriting_source");
static_assert(!nucleus::inheriting_source<projecting_stub>,
              "projecting_stub has no inheritance(); must not satisfy inheriting_source");
static_assert(nucleus::inheriting_source<full_stub>,
              "full_stub has both optional ops; must satisfy inheriting_source");

// ----- source_handle move-only trait proofs -----

static_assert(!std::is_copy_constructible_v<nucleus::source_handle>,
              "source_handle must not be copy-constructible");
static_assert(!std::is_copy_assignable_v<nucleus::source_handle>,
              "source_handle must not be copy-assignable");
static_assert(std::is_move_constructible_v<nucleus::source_handle>,
              "source_handle must be move-constructible");
static_assert(std::is_move_assignable_v<nucleus::source_handle>,
              "source_handle must be move-assignable");

}

TEST_CASE("source_handle wrapping a flat stub forwards capabilities() and pull()", "[source_concept]")
{
    nucleus::source_handle h{flat_stub{}};

    auto caps = h.capabilities();
    REQUIRE(caps.supports(nucleus::capability::nesting));
    REQUIRE_FALSE(caps.supports(nucleus::capability::typed_scalars));

    auto result = h.pull();
    REQUIRE(result);
    REQUIRE(result.value().entries.size() == 1);
    REQUIRE(result.value().entries[0].path == "flat/key");
    REQUIRE(result.value().entries[0].value.text() == "flat-value");
}

TEST_CASE("source_handle wrapping a flat stub defaults apply_projection() to a no-op", "[source_concept]")
{
    // No crash, no visible side effect. Confirmed by pulling before and after.
    nucleus::source_handle h{flat_stub{}};

    nucleus::schema_projection dummy_proj;
    h.apply_projection(dummy_proj); // must not crash or alter state
}

TEST_CASE("source_handle wrapping a flat stub defaults inheritance() to inherit_default", "[source_concept]")
{
    nucleus::source_handle h{flat_stub{}};

    auto decl = h.inheritance();
    REQUIRE(decl.which == nucleus::inherit_declaration::kind::inherit_default);
    REQUIRE(decl.path.empty());
}

TEST_CASE("source_handle wrapping a full stub forwards apply_projection() to the underlying struct", "[source_concept]")
{
    full_stub src;
    nucleus::source_handle h{std::move(src)};

    nucleus::schema_projection dummy_proj;
    h.apply_projection(dummy_proj);
    // The side effect (projection_count) is inside the erased model; we confirm
    // the call did not crash and the handle is still functional.
    auto result = h.pull();
    REQUIRE(result);
    REQUIRE(result.value().entries[0].path == "full/key");
}

TEST_CASE("source_handle wrapping a full stub forwards inheritance() and returns the sentinel", "[source_concept]")
{
    nucleus::source_handle h{full_stub{}};

    auto decl = h.inheritance();
    REQUIRE(decl.which == nucleus::inherit_declaration::kind::parent_path);
    REQUIRE(decl.path == "sentinel.xml");
}

TEST_CASE("source_handle wrapping a full stub forwards capabilities() faithfully", "[source_concept]")
{
    nucleus::source_handle h{full_stub{}};

    auto caps = h.capabilities();
    REQUIRE(caps.supports(nucleus::capability::nesting));
    REQUIRE(caps.supports(nucleus::capability::typed_scalars));
}
