#include "nucleus/expected.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <memory>
#include <cstddef>
#include <utility>

using nucleus::expected;
using nucleus::unexpect;

namespace {

expected<int, std::string> parse(bool ok)
{
    if (ok) return 42;
    return nucleus::unexpected<std::string>("bad input");
}

}

TEST_CASE("expected carries a value on success", "[expected]")
{
    auto r = parse(true);
    REQUIRE(r.has_value());
    REQUIRE(static_cast<bool>(r));
    REQUIRE(r.value() == 42);
    REQUIRE(*r == 42);
    REQUIRE(*r.operator->() == 42);
}

TEST_CASE("expected carries an error on failure", "[expected]")
{
    auto r = parse(false);
    REQUIRE_FALSE(r.has_value());
    REQUIRE_FALSE(static_cast<bool>(r));
    REQUIRE(r.error() == "bad input");
}

TEST_CASE("expected disambiguates same-typed value and error via unexpected", "[expected]")
{
    expected<int, int> ok(5);
    expected<int, int> err(nucleus::unexpected(9));
    REQUIRE(ok.has_value());
    REQUIRE(ok.value() == 5);
    REQUIRE_FALSE(err.has_value());
    REQUIRE(err.error() == 9);
}

TEST_CASE("expected converts the error on construction from unexpected", "[expected]")
{
    expected<int, std::string> e(nucleus::unexpected("literal"));
    REQUIRE_FALSE(e.has_value());
    REQUIRE(e.error() == std::string("literal"));
}

TEST_CASE("value_or returns the value or the fallback", "[expected]")
{
    REQUIRE(parse(true).value_or(0) == 42);
    REQUIRE(parse(false).value_or(7) == 7);

    const expected<int, std::string> ok(1);
    const expected<int, std::string> err(nucleus::unexpected<std::string>("e"));
    REQUIRE(ok.value_or(99) == 1);
    REQUIRE(err.value_or(99) == 99);
}

TEST_CASE("error_or returns the error or the fallback", "[expected]")
{
    const expected<int, std::string> ok(1);
    const expected<int, std::string> err(nucleus::unexpected<std::string>("boom"));
    REQUIRE(ok.error_or("none") == "none");
    REQUIRE(err.error_or("none") == "boom");
    REQUIRE(expected<int, std::string>(nucleus::unexpected<std::string>("rv")).error_or("none") == "rv");
}

TEST_CASE("and_then invokes on success and short-circuits on error", "[expected]")
{
    bool called = false;
    auto twice = [&called](int v) {
        called = true;
        return expected<int, std::string>(v * 2);
    };

    auto ok = parse(true).and_then(twice);
    REQUIRE(called);
    REQUIRE(ok.has_value());
    REQUIRE(ok.value() == 84);

    called = false;
    auto err = parse(false).and_then(twice);
    REQUIRE_FALSE(called);
    REQUIRE_FALSE(err.has_value());
    REQUIRE(err.error() == "bad input");
}

TEST_CASE("or_else invokes on error and short-circuits on success", "[expected]")
{
    bool called = false;
    auto recover = [&called](const std::string &) {
        called = true;
        return expected<int, std::string>(0);
    };

    auto ok = parse(true).or_else(recover);
    REQUIRE_FALSE(called);
    REQUIRE(ok.has_value());
    REQUIRE(ok.value() == 42);

    called = false;
    auto err = parse(false).or_else(recover);
    REQUIRE(called);
    REQUIRE(err.has_value());
    REQUIRE(err.value() == 0);
}

TEST_CASE("transform maps the value and propagates the error", "[expected]")
{
    auto ok = parse(true).transform([](int v) { return std::to_string(v); });
    REQUIRE(ok.has_value());
    REQUIRE(ok.value() == "42");

    auto err = parse(false).transform([](int v) { return std::to_string(v); });
    REQUIRE_FALSE(err.has_value());
    REQUIRE(err.error() == "bad input");
}

TEST_CASE("transform_error maps the error and propagates the value", "[expected]")
{
    auto err = parse(false).transform_error([](const std::string &e) { return e.size(); });
    REQUIRE_FALSE(err.has_value());
    REQUIRE(err.error() == std::size_t{9});

    auto ok = parse(true).transform_error([](const std::string &e) { return e.size(); });
    REQUIRE(ok.has_value());
    REQUIRE(ok.value() == 42);
}

TEST_CASE("value/error/and_then work across all four ref-qualifications", "[expected]")
{
    expected<int, std::string> lvalue(10);
    const expected<int, std::string> clvalue(11);

    REQUIRE(lvalue.value() == 10);          // non-const lvalue
    REQUIRE(clvalue.value() == 11);         // const lvalue
    REQUIRE(expected<int, std::string>(12).value() == 12);  // rvalue

    auto wrap = [](int v) { return expected<int, std::string>(v + 1); };
    REQUIRE(lvalue.and_then(wrap).value() == 11);
    REQUIRE(clvalue.and_then(wrap).value() == 12);
    REQUIRE(expected<int, std::string>(20).and_then(wrap).value() == 21);

    const expected<int, std::string> cerr(nucleus::unexpected<std::string>("e"));
    REQUIRE(cerr.error() == "e");
    REQUIRE(expected<int, std::string>(nucleus::unexpected<std::string>("rv")).error() == "rv");
}

TEST_CASE("a move-only value flows through the monadic chain without a copy", "[expected]")
{
    expected<std::unique_ptr<int>, std::string> src(std::make_unique<int>(7));

    auto out = std::move(src)
                   .and_then([](std::unique_ptr<int> p) {
                       return expected<std::unique_ptr<int>, std::string>(std::move(p));
                   })
                   .transform([](std::unique_ptr<int> p) { return *p; });

    REQUIRE(out.has_value());
    REQUIRE(out.value() == 7);
}

TEST_CASE("expected<void, E> tracks success and error", "[expected]")
{
    expected<void, std::string> ok{};
    REQUIRE(ok.has_value());
    REQUIRE(static_cast<bool>(ok));

    expected<void, std::string> err(nucleus::unexpected<std::string>("void-err"));
    REQUIRE_FALSE(err.has_value());
    REQUIRE(err.error() == "void-err");
}

TEST_CASE("expected<void, E> chains with nullary and void-returning callables", "[expected]")
{
    bool ran = false;
    expected<void, std::string> ok{};

    auto chained = ok.and_then([]() { return expected<int, std::string>(5); })
                       .transform([&ran](int v) { ran = true; (void)v; });

    REQUIRE(ran);
    REQUIRE(chained.has_value());

    auto recovered = expected<void, std::string>(nucleus::unexpected<std::string>("x"))
                         .or_else([](const std::string &) { return expected<void, std::string>(); });
    REQUIRE(recovered.has_value());

    auto mapped_err = expected<void, std::string>(nucleus::unexpected<std::string>("err"))
                          .transform_error([](const std::string &e) { return e.size(); });
    REQUIRE_FALSE(mapped_err.has_value());
    REQUIRE(mapped_err.error() == std::size_t{3});
}

TEST_CASE("expected equality compares value and error states", "[expected]")
{
    expected<int, std::string> a(1);
    expected<int, std::string> b(1);
    expected<int, std::string> c(nucleus::unexpected<std::string>("e"));
    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
    REQUIRE(c == nucleus::unexpected<std::string>("e"));
}
