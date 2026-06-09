#include "nucleus/capability.h"

#include "nucleus/configuration_source/source_concept.h"
#include "nucleus/configuration_source/source_handle.h"
#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

// A hand-written plain struct source. It emits owned entries directly -- the
// "I write a plain struct satisfying the concept" authoring path. No inheritance,
// no virtuals.
struct handwritten_source
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::nesting};
    }

    [[nodiscard]] nucleus::configuration_source_result pull()
    {
        nucleus::configuration_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "a/b", nucleus::value::owned("from-handwritten"), capabilities()));
        return batch;
    }
};

static_assert(nucleus::source_satisfies<handwritten_source>,
              "handwritten_source must satisfy the source concept");

// A second plain struct source: a different author, different capabilities and
// entries. Both reach the engine through the SAME source_handle erasure path --
// which is what proves the seam is real rather than a single-source stub.
struct ordering_source
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::ordering};
    }

    [[nodiscard]] nucleus::configuration_source_result pull()
    {
        nucleus::configuration_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "x/y", nucleus::value::owned("from-ordering"), capabilities()));
        return batch;
    }
};

static_assert(nucleus::source_satisfies<ordering_source>,
              "ordering_source must satisfy the source concept");

// Drives any erased source through source_handle -- it cannot see whether the
// source was handwritten or any other plain struct, proving the seam is opaque.
std::vector<nucleus::keyspace_entry> drive(nucleus::source_handle &handle)
{
    auto pulled = handle.pull();
    REQUIRE(pulled);
    return std::move(pulled.value().entries);
}

}

TEST_CASE("two plain-struct sources share one erasure path through source_handle", "[source]")
{
    nucleus::source_handle a{handwritten_source{}};
    nucleus::source_handle b{ordering_source{}};

    auto entries_a = drive(a);
    auto entries_b = drive(b);

    REQUIRE(entries_a.size() == 1);
    REQUIRE(entries_a[0].path == "a/b");
    REQUIRE(entries_a[0].value.text() == "from-handwritten");

    REQUIRE(entries_b.size() == 1);
    REQUIRE(entries_b[0].path == "x/y");
    REQUIRE(entries_b[0].value.text() == "from-ordering");
}

TEST_CASE("source_handle forwards the capability descriptor", "[source]")
{
    nucleus::source_handle h{handwritten_source{}};
    auto caps = h.capabilities();
    REQUIRE(caps.supports(nucleus::capability::nesting));
    REQUIRE_FALSE(caps.supports(nucleus::capability::ordering));
}

TEST_CASE("a view value aliases its backing text; an owned value is self-contained", "[source]")
{
    std::string backing = "retained-bytes";
    auto v = nucleus::value::view(backing);
    REQUIRE(v.is_view());
    REQUIRE(v.text() == "retained-bytes");

    auto copied = v.to_owned();
    REQUIRE(copied.is_owned());
    backing = "mutated";          // the owned copy is severed from the backing.
    REQUIRE(copied.text() == "retained-bytes");
}
