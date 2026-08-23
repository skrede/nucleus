// capability_gating: load auto-gates with NO host gate call.
//
// The schema is nested (a `server` container primary-keyed by `name`) and typed
// (a `std::uint16_t` `port`), so it requires the `nesting` capability. A flat env source
// cannot represent nesting, so the load fails loudly BEFORE folding -- the gate is
// part of every load, not a separate step the host must remember to call.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/env/env_source.h"

#include <string>
#include <cstdint>
#include <utility>
#include <iostream>

template<typename Builder>
static nucleus::registration_result define_space(Builder &builder)
{
    if(auto result = builder.register_element(nucleus::element("server", nucleus::anchor::root()));
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::primary_key_element("name", nucleus::anchor::keyspace("server")));
       !result)
        return result;
    return builder.register_element(
            nucleus::typed_element<std::uint16_t>("port", nucleus::anchor::keyspace("server")));
}

template<typename Builder>
static nucleus::expected<nucleus::config_space, nucleus::error> make_space(Builder &builder)
{
    if(auto result = define_space(builder); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

static nucleus::expected<nucleus::config_space, nucleus::error> make_space()
{
    nucleus::config_space_builder builder;
    return make_space(builder);
}

static bool is_missing_nesting(const nucleus::error &error)
{
    return error.code == nucleus::errc::unmet_capability &&
            error.message.find(
                    "no source can satisfy capability 'nesting' required by 'schema'") !=
            std::string::npos;
}

static int report_capability_rejection(nucleus::load_result loaded,
                                       std::ostream        &output,
                                       std::ostream        &errors)
{
    if(loaded)
    {
        errors << "unexpected success: flat source accepted\n";
        return 1;
    }
    if(!is_missing_nesting(loaded.error()))
    {
        errors << "unexpected rejection: " << loaded.error() << '\n';
        return 1;
    }
    output << "load auto-gated and refused: " << loaded.error() << '\n';
    return 0;
}

static int run_gating_example(nucleus::expected<nucleus::config_space, nucleus::error> space,
                              std::ostream                                            &output,
                              std::ostream                                            &errors)
{
    if(!space)
    {
        errors << "space setup failed: " << space.error() << '\n';
        return 1;
    }

    nucleus::env_source values;
    values.set("server/name", "primary");

    auto loaded = nucleus::load_config(*space, nucleus::source_stack{std::move(values)}, {});
    return report_capability_rejection(std::move(loaded), output, errors);
}

int main()
{
    return run_gating_example(make_space(), std::cout, std::cerr);
}
