// reusable_space: the space is the authority on layout, reused across many source stacks.
// Each load yields a disconnected config; the space itself is never modified.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <vector>
#include <iostream>

static bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(
                   nucleus::element("server", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::element("host", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::element("port", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::enum_element(
                            "mode", nucleus::anchor::keyspace("server"),
                            std::vector<std::string>{"primary", "secondary"}));
}

static nucleus::runtime_source make_primary_source()
{
    nucleus::runtime_source primary_src;
    primary_src.set("server/host", "primary-host")
            .set("server/port", "8000")
            .set("server/mode", "primary");
    return primary_src;
}

static nucleus::runtime_source make_secondary_source()
{
    nucleus::runtime_source secondary_src;
    secondary_src.set("server/host", "secondary-host")
            .set("server/port", "9000")
            .set("server/mode", "secondary");
    return secondary_src;
}

// Both configurations are disconnected and simultaneously readable after the
// source stacks that produced them are gone.
static void print_profiles(const nucleus::config &primary,
                           const nucleus::config &secondary)
{
    std::cout << "primary profile:\n";
    for(const std::string &key : primary.keys())
        std::cout << "  " << key << " = " << primary.get(key).value() << '\n';

    std::cout << "secondary profile:\n";
    for(const std::string &key : secondary.keys())
        std::cout << "  " << key << " = " << secondary.get(key).value() << '\n';

    // Same key set (same space), different values (different stacks).
    std::cout << "\nkey sets match:  " << (primary.keys() == secondary.keys() ? "yes" : "no") << '\n';
    std::cout << "values differ:   "
              << (primary.get("server/host") != secondary.get("server/host") ? "yes" : "no") << '\n';
}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    const nucleus::config_space space          = builder.build();
    auto                        loaded_primary = nucleus::load_config(
            space, nucleus::source_stack{make_primary_source()}, {});
    auto loaded_secondary = nucleus::load_config(
            space, nucleus::source_stack{make_secondary_source()}, {});
    if(!loaded_primary)
    {
        std::cerr << "primary load failed: " << loaded_primary.error() << '\n';
        return 1;
    }
    if(!loaded_secondary)
    {
        std::cerr << "secondary load failed: " << loaded_secondary.error() << '\n';
        return 1;
    }
    const nucleus::config primary   = std::move(loaded_primary).value();
    const nucleus::config secondary = std::move(loaded_secondary).value();
    print_profiles(primary, secondary);
    return 0;
}
