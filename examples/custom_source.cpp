// custom_source: implement the `source` seam by subclassing it.
//
// A source declares its capabilities() and yields entries from pull(). This one
// emits owned values, so it pins no buffer (retained_buffer::none()). Any source
// -- argv, env, XML, or this -- reaches the engine through this one interface.

#include "nucleus/configuration_space.h"

#include "nucleus/capability.h"
#include "nucleus/entry/precedence.h"

#include "nucleus/source/source.h"
#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <iostream>

namespace {

// A source backed by an in-memory table. It owns its values, so views never
// outlive a backing buffer and no retained_buffer is needed.
class table_source final : public nucleus::source
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
            "service/name", nucleus::value::owned("edge"), capabilities()));
        batch.entries.push_back(nucleus::make_entry(
            "service/region", nucleus::value::owned("eu-west"), capabilities()));
        return batch;
    }
};

}

int main()
{
    table_source source;

    nucleus::source_stack stack;
    stack.add(source, nucleus::layer_rank::base, "table");

    nucleus::configuration_space engine;
    auto loaded = engine.resolve(stack);
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';
    return 0;
}
