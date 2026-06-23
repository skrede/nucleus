// reusable_space: the space is the authority on layout, reused across many source stacks.
// Each load yields a disconnected config; the space itself is never modified.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <vector>
#include <iostream>

int main()
{
    // One sealed space: the shared authority on layout for every profile.
    nucleus::config_space_builder builder;
    if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(nucleus::element("host", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(nucleus::element("port", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(
        nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                              std::vector<std::string>{"primary", "secondary"})))
        return 1;
    const nucleus::config_space space = builder.build();

    // Primary profile stack: its own source, its own values.
    nucleus::runtime_source primary_src;
    primary_src.set("server/host", "primary-host")
               .set("server/port", "8000")
               .set("server/mode", "primary");

    // Secondary profile stack: different source, different values, same space.
    nucleus::runtime_source secondary_src;
    secondary_src.set("server/host", "secondary-host")
                 .set("server/port", "9000")
                 .set("server/mode", "secondary");

    // Two loads from the same space -- stacks are swapped, not the space.
    auto loaded_primary   = nucleus::load_config(space, nucleus::source_stack{std::move(primary_src)},   {});
    auto loaded_secondary = nucleus::load_config(space, nucleus::source_stack{std::move(secondary_src)}, {});

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

    // Both configurations are disconnected and simultaneously readable.
    // The stacks that produced them are already gone.
    const nucleus::config primary   = std::move(loaded_primary).value();
    const nucleus::config secondary = std::move(loaded_secondary).value();

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

    return 0;
}
