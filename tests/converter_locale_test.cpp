// Forced-fallback locale regression for the built-in float converters.
//
// This TU is compiled with NUCLEUS_FORCE_FP_FROM_CHARS_FALLBACK so the
// strtof/strtod fallback path (live on Apple libc++, otherwise shadowed by
// std::from_chars) is exercised on every platform. It then switches the thread
// numeric locale to a comma-decimal locale and proves the fallback still parses
// "3.14" as 3.14 -- not 3 -- and rejects "3,5" and "0x10" as trailing characters,
// byte-identical to the primary from_chars path. Under the pre-fix strtod the
// "3.14" assertion fails (the comma locale truncates it to 3); under the cached
// "C" locale _l primitives it passes.
//
// If no comma-decimal locale is installable the test SKIPs, so it can never
// false-fail on a runner lacking the locale (CTest maps Catch2's exit 4 to a
// skip via SKIP_RETURN_CODE). macOS exercises the real, non-forced fallback as
// the backstop.

#include "nucleus/schema/converters.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <array>
#include <string>
#include <string_view>

// <clocale> lacks newlocale/uselocale/strtod_l and the MSVC _l equivalents.
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <locale.h>

using nucleus::make_scalar_converter;

namespace {

#if defined(_MSC_VER)
constexpr std::array<const char *, 4> comma_locale_candidates{
    "de-DE", "German_Germany", "fr-FR", "nl-NL"};
#else
constexpr std::array<const char *, 4> comma_locale_candidates{
    "de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "nl_NL.UTF-8"};
#endif

// Asserts that, under whatever comma-decimal thread locale is active, the float
// converter for T reproduces the primary from_chars behavior exactly.
template<typename T>
void assert_locale_independent()
{
    const auto conv = make_scalar_converter<T>();

    const auto dot = conv("3.14");
    REQUIRE(dot);
    // A comma locale would truncate "3.14" to 3 on a naive strtod; the cached
    // "C" locale parse must reproduce 3.14 bit-for-bit -- the load-bearing check.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
    REQUIRE(std::any_cast<T>(dot.value()) == static_cast<T>(3.14L));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    const auto comma = conv("3,5");
    REQUIRE(!comma);
    REQUIRE(comma.error().find("trailing") != std::string::npos);

    const auto hex = conv("0x10");
    REQUIRE(!hex);
    REQUIRE(hex.error().find("trailing") != std::string::npos);
}

#if defined(_MSC_VER)

// Restores the thread numeric locale on every exit path (including a REQUIRE
// throw) so the comma locale cannot bleed into sibling Catch2 tests.
struct thread_numeric_locale_guard
{
    std::string previous;
    thread_numeric_locale_guard()
    {
        _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
        const char *const cur = setlocale(LC_NUMERIC, nullptr);
        previous = (cur != nullptr) ? cur : "C";
    }
    bool install(const char *name) { return setlocale(LC_NUMERIC, name) != nullptr; }
    ~thread_numeric_locale_guard() { setlocale(LC_NUMERIC, previous.c_str()); }
    thread_numeric_locale_guard(const thread_numeric_locale_guard &) = delete;
    thread_numeric_locale_guard &operator=(const thread_numeric_locale_guard &) = delete;
    thread_numeric_locale_guard(thread_numeric_locale_guard &&) = delete;
    thread_numeric_locale_guard &operator=(thread_numeric_locale_guard &&) = delete;
};

#else

// POSIX equivalent: newlocale acquires the comma locale (also the availability
// probe), uselocale installs it thread-scoped, and the destructor restores the
// previous thread locale and frees the handle on every exit path.
struct thread_numeric_locale_guard
{
    locale_t installed{nullptr};
    locale_t previous{nullptr};
    bool install(const char *name)
    {
        const locale_t loc = newlocale(LC_NUMERIC_MASK, name, nullptr);
        if(loc == nullptr)
            return false;
        installed = loc;
        previous = uselocale(loc);
        return true;
    }
    ~thread_numeric_locale_guard()
    {
        if(installed != nullptr)
        {
            uselocale(previous);
            freelocale(installed);
        }
    }
    thread_numeric_locale_guard() = default;
    thread_numeric_locale_guard(const thread_numeric_locale_guard &) = delete;
    thread_numeric_locale_guard &operator=(const thread_numeric_locale_guard &) = delete;
    thread_numeric_locale_guard(thread_numeric_locale_guard &&) = delete;
    thread_numeric_locale_guard &operator=(thread_numeric_locale_guard &&) = delete;
};

#endif

}

TEST_CASE("float fallback parses locale-independently under a comma-decimal locale",
          "[typed][builtin][float][locale]")
{
    thread_numeric_locale_guard guard;

    bool installed = false;
    for(const char *const name : comma_locale_candidates)
        if(guard.install(name))
        {
            installed = true;
            break;
        }

    if(!installed)
        SKIP("no comma-decimal locale installed "
             "(tried de_DE.UTF-8/de_DE/fr_FR.UTF-8/nl_NL.UTF-8)");

    assert_locale_independent<float>();
    assert_locale_independent<double>();
}
