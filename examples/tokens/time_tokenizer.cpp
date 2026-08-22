// Authoring a custom tokenizer: a host-defined ${time.*} category.
//
// This is the canonical example of how a host AUTHORS and INSTALLS its own
// tokenizer through the public seam -- the core ships only env/string and the
// mechanism; the vocabulary is yours. Time is a deliberate choice of subject:
// it is non-deterministic and environment-derived, exactly the class of value
// that does NOT belong in the mechanism-only core (the same reasoning that
// keeps uuid/host vocabulary out). It lives here, in a host, behind the seam.
//
// The lesson is the clock INJECTION. A tokenizer that read the wall clock
// directly would resolve to a different value on every load -- impossible to
// test, and at odds with nucleus's resolve-at-load contract. Instead the
// factory takes the instant as a parameter: production passes
// system_clock::now(); a test (and this demo) passes a fixed instant, so the
// resolved configuration is deterministic and the tokenizer is trivially
// verifiable. Injecting the clock is what lets a wall-clock concern live
// cleanly behind a load-time resolver.
//
// Formatting uses strftime, not std::chrono's formatter or the tz database:
// those lag on the supported compiler floor, while strftime is everywhere.
// Two functions, local and utc, each take an optional named `format=` argument
// declared as a string with a default -- so ${time.utc()} uses the ISO default
// and ${time.utc(format='%Y')} overrides it. This is the named/typed argument
// surface: the author declares the argument once; the framework binds it.

#include "nucleus/config_space.h"
#include "nucleus/tokenizer/named_args.h"
#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/resolve_error.h"
#include "nucleus/tokenizer/tokenizer_builder.h"

#include "nucleus/env/env_source.h"

#include <array>
#include <ctime>
#include <chrono>
#include <string>
#include <iostream>

namespace {

// Renders one instant via strftime. utc selects gmtime vs localtime; the tm is
// copied out of strftime's static storage before use.
nucleus::token_result render(std::chrono::system_clock::time_point instant,
                             bool utc, const std::string &format)
{
    std::time_t epoch = std::chrono::system_clock::to_time_t(instant);
    std::tm     parts = utc ? *std::gmtime(&epoch) : *std::localtime(&epoch);

    std::array<char, 128> buffer{};
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    // format is a host-supplied named tokenizer argument (${time.utc(format=...)}),
    // genuinely not a compile-time literal by design.
    std::size_t written = std::strftime(buffer.data(), buffer.size(), format.c_str(), &parts);
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
    if(written == 0)
        return nucleus::unexpected(nucleus::resolve_error(
                nucleus::resolve_errc::parse_error,
                "time format produced no output or exceeded the render buffer: " + format));
    return std::string(buffer.data(), written);
}

// Builds the ${time.local(...)} / ${time.utc(...)} tokenizer anchored to one
// instant. The instant is the injection seam -- the only source of "now".
nucleus::tokenizer make_time_tokenizer(std::chrono::system_clock::time_point instant)
{
    nucleus::tokenizer_builder builder("time");

    auto clock_fn = [instant](bool utc)
    {
        return [instant, utc](const nucleus::named_args &args) -> nucleus::token_result
        {
            return render(instant, utc, args.string("format"));
        };
    };

    using nucleus::arg_spec;
    using nucleus::arg_type;
    builder.add_function("utc",
                         {arg_spec::scalar("format", arg_type::string).with_default("%Y-%m-%dT%H:%M:%SZ")},
                         clock_fn(true));
    builder.add_function("local",
                         {arg_spec::scalar("format", arg_type::string).with_default("%Y-%m-%dT%H:%M:%S")},
                         clock_fn(false));
    return std::move(builder).build();
}

nucleus::env_source make_fixed_values()
{
    nucleus::env_source values;
    values.set("build/stamp", "${time.utc()}")
            .set("build/date", "${time.utc(format='%Y-%m-%d')}")
            .set("build/local", "${time.local(format='%H:%M:%S')}")
            .set("build/weekday", "${string.upper(value=${time.utc(format='%A')})}")
            // A list argument: a literal joined with a nested ${...} element.
            .set("build/label", "${string.concat(values=['build', ${time.utc(format='%Y')}], separator='-')}");
    return values;
}

// build/local reflects the host time zone; the utc-derived keys are stable.
void print_values(const nucleus::config &config)
{
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';
}

}

int main()
{
    // Inject the clock. Swap this single line for
    // std::chrono::system_clock::now() in production; the fixed instant here
    // makes the utc output below deterministic and the tokenizer testable.
    const auto                    instant = std::chrono::system_clock::from_time_t(1700000000);
    nucleus::env_source           values  = make_fixed_values();
    nucleus::config_space_builder builder;
    if(auto installed = builder.install_tokenizer(make_time_tokenizer(instant)); !installed)
    {
        std::cerr << "install failed: " << installed.error() << '\n';
        return 1;
    }
    nucleus::config_space space = builder.build();

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(values)}, {});
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }
    print_values(loaded.value());
    return 0;
}
