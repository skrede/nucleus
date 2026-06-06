#include "nucleus/capability.h"

#include "nucleus/source/parser.h"
#include "nucleus/source/source.h"
#include "nucleus/source/parser_adapter.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

// A hand-written runtime-virtual source. It emits owned entries directly -- the
// "I subclass the interface myself" authoring path.
class handwritten_source final : public nucleus::source
{
public:
    [[nodiscard]] nucleus::capability_descriptor capabilities() const override
    {
        return {nucleus::capability::nesting};
    }

    [[nodiscard]] nucleus::source_result pull() override
    {
        nucleus::source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "a/b", nucleus::value::owned("from-virtual"), capabilities()));
        return batch;
    }
};

// A fake Parser-concept struct: a plain value type, no inheritance, no virtuals.
// It satisfies the Parser concept by declaring capabilities() and pull(). It is
// the proof that an author can write a struct (not a subclass) and have it reach
// the engine through the SAME virtual path -- without pugixml.
struct fake_parser
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::ordering};
    }

    [[nodiscard]] nucleus::source_result pull() const
    {
        nucleus::source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "x/y", nucleus::value::owned("from-parser"), capabilities()));
        return batch;
    }
};

static_assert(nucleus::Parser<fake_parser>,
              "the fake parser must satisfy the Parser concept");

// Drives any source through the abstract virtual interface only -- it cannot see
// whether the source was hand-written or adapter-wrapped.
std::vector<nucleus::keyspace_entry> drive(nucleus::source &src)
{
    auto pulled = src.pull();
    REQUIRE(pulled);
    return std::move(pulled.value().entries);
}

}

TEST_CASE("a hand-written source and an adapted fake parser share one virtual path", "[source]")
{
    handwritten_source virtual_src;
    auto adapted = nucleus::adapt_parser(fake_parser{});

    // Both are reached only as `source&` -- the same abstract pull path.
    std::vector<nucleus::source *> sources{&virtual_src, adapted.get()};

    auto a = drive(*sources[0]);
    auto b = drive(*sources[1]);

    REQUIRE(a.size() == 1);
    REQUIRE(a[0].path == "a/b");
    REQUIRE(a[0].value.text() == "from-virtual");

    REQUIRE(b.size() == 1);
    REQUIRE(b[0].path == "x/y");
    REQUIRE(b[0].value.text() == "from-parser");
}

TEST_CASE("the adapter preserves the parser's capability descriptor", "[source]")
{
    auto adapted = nucleus::adapt_parser(fake_parser{});
    nucleus::source &as_source = *adapted;

    auto caps = as_source.capabilities();
    REQUIRE(caps.supports(nucleus::capability::ordering));
    REQUIRE_FALSE(caps.supports(nucleus::capability::nesting));
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
