#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <utility>
#include <iostream>

namespace {

nucleus::expected<nucleus::config_space, nucleus::error> make_space()
{
    nucleus::config_space_builder engine;
    if(auto result = engine.register_element(
            nucleus::element("server", nucleus::anchor::root())); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = engine.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("server"))); !result)
        return nucleus::unexpected(std::move(result).error());
    return engine.build();
}

bool read_back(const nucleus::config &config)
{
    if(config.get("server/name") != "edge")
    {
        std::cerr << "text accessor mismatch\n";
        return false;
    }
    return true;
}

bool consume_runtime_stack()
{
    const auto sealed = make_space();
    if(!sealed)
    {
        std::cerr << "schema registration rejected: " << sealed.error() << '\n';
        return false;
    }

    nucleus::runtime_source values;
    values.set("server/name", "edge");

    auto loaded = nucleus::load_config(sealed.value(),
        nucleus::source_stack{std::move(values)},
        {});
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return false;
    }
    return read_back(loaded.value());
}

}

int main()
{
    return consume_runtime_stack() ? 0 : 1;
}
