// Exhaustive error-path coverage for the built-in scalar converters.
//
// typed_element_test.cpp covers the happy paths and a sampling of edge cases
// per type; this suite drives every numeric converter through the full failure
// matrix the dispatch in converters.h distinguishes:
//   empty input, trailing characters, out-of-range, a leading sign rejected as
//   out-of-range, and pure non-numeric input rejected as invalid characters.
// Together they exercise each branch of every integral and floating-point
// converter, including the otherwise-untested int16/int64/uint16/uint64 widths.

#include "nucleus/schema/converters.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <string>
#include <cstdint>
#include <string_view>

using nucleus::make_scalar_converter;

namespace {

// Asserts that `input` converts successfully to the value `expected`.
template<typename T>
void converts_to(std::string_view input, T expected)
{
    auto conv = make_scalar_converter<T>();
    auto r = conv(input);
    REQUIRE(r);
    // Exact equality is the contract under test: a converter must reproduce the
    // parsed value bit-for-bit, so for floating T this comparison is deliberate.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
    REQUIRE(std::any_cast<T>(r.value()) == expected);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

// Asserts that `input` fails conversion with an error containing `reason`.
template<typename T>
void fails_with(std::string_view input, std::string_view reason)
{
    auto conv = make_scalar_converter<T>();
    auto r = conv(input);
    REQUIRE(!r);
    REQUIRE(r.error().find(std::string(reason)) != std::string::npos);
}

// Drives a signed integral converter through every dispatch branch. A leading
// '+' is rejected by from_chars with zero characters consumed and reported as
// out-of-range (sign-into-type), distinct from pure non-numeric input.
template<typename Signed>
void exercise_signed_integral(std::string_view valid_text, Signed valid_value,
                              std::string_view overflow_text)
{
    converts_to<Signed>(valid_text, valid_value);
    fails_with<Signed>("", "empty");
    fails_with<Signed>(std::string(valid_text) + "xyz", "trailing");
    fails_with<Signed>(overflow_text, "out of range");
    fails_with<Signed>("+5", "invalid characters");
    fails_with<Signed>("abc", "invalid characters");
}

// Drives an unsigned integral converter through every branch. A leading '-' is
// the sign-into-unsigned case reported as out-of-range.
template<typename Unsigned>
void exercise_unsigned_integral(std::string_view valid_text, Unsigned valid_value,
                                std::string_view overflow_text)
{
    converts_to<Unsigned>(valid_text, valid_value);
    fails_with<Unsigned>("", "empty");
    fails_with<Unsigned>(std::string(valid_text) + "xyz", "trailing");
    fails_with<Unsigned>(overflow_text, "out of range");
    fails_with<Unsigned>("-5", "out of range");
    fails_with<Unsigned>("abc", "invalid characters");
}

}

TEST_CASE("signed integral converters cover the full error matrix", "[typed][builtin][integer]")
{
    SECTION("int8_t")  { exercise_signed_integral<int8_t>("42", 42, "999"); }
    SECTION("int16_t") { exercise_signed_integral<int16_t>("1234", 1234, "99999"); }
    SECTION("int32_t") { exercise_signed_integral<int32_t>("100000", 100000, "9999999999"); }
    SECTION("int64_t") { exercise_signed_integral<int64_t>("5000000000", 5000000000LL,
                                                           "99999999999999999999"); }
}

TEST_CASE("unsigned integral converters cover the full error matrix", "[typed][builtin][integer]")
{
    SECTION("uint8_t")  { exercise_unsigned_integral<uint8_t>("200", 200, "256"); }
    SECTION("uint16_t") { exercise_unsigned_integral<uint16_t>("50000", 50000, "65536"); }
    SECTION("uint32_t") { exercise_unsigned_integral<uint32_t>("4000000000", 4000000000U, "9999999999"); }
    SECTION("uint64_t") { exercise_unsigned_integral<uint64_t>("10000000000", 10000000000ULL,
                                                               "99999999999999999999999999"); }
}

TEST_CASE("float converter covers the full error matrix", "[typed][builtin][float]")
{
    converts_to<float>("3.5", 3.5f);
    fails_with<float>("", "empty");
    fails_with<float>("3.5xyz", "trailing");
    fails_with<float>("1e999", "out of range");
    fails_with<float>("+3.5", "invalid characters");
    fails_with<float>("abc", "invalid characters");
}

TEST_CASE("double converter covers the full error matrix", "[typed][builtin][float]")
{
    converts_to<double>("2.5", 2.5);
    fails_with<double>("", "empty");
    fails_with<double>("2.5xyz", "trailing");
    fails_with<double>("1e999", "out of range");
    fails_with<double>("+2.5", "invalid characters");
    fails_with<double>("abc", "invalid characters");
}

// A leading '-' with nothing parseable after it is syntax, not range, for signed
// and floating targets: "-abc" and a bare "-" are invalid characters. Only an
// unsigned target reads a leading '-' as a value below its floor (out of range).
TEST_CASE("a leading minus is invalid characters for signed and float targets",
          "[typed][builtin][integer][float]")
{
    fails_with<int32_t>("-abc", "invalid characters");
    fails_with<int32_t>("-", "invalid characters");
    fails_with<int64_t>("-abc", "invalid characters");
    fails_with<float>("-abc", "invalid characters");
    fails_with<float>("-", "invalid characters");
    fails_with<double>("-abc", "invalid characters");

    fails_with<uint32_t>("-abc", "out of range");
    fails_with<uint32_t>("-", "out of range");
}
