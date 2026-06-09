// custom_source: implement the `source` seam by subclassing it.
//
// A source declares its capabilities() and yields entries from pull(). This one
// emits owned values, so it pins no buffer (retained_buffer::none()). Any source
// -- argv, env, XML, or this -- reaches the engine through this one interface.

#include "nucleus/configuration_space.h"

#include "nucleus/capability.h"

#include "nucleus/configuration_source/configuration_source.h"
#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <iostream>

namespace {

// A source backed by an in-memory table. It owns its values, so views never
// outlive a backing buffer and no retained_buffer is needed.
class table_source final : public nucleus::configuration_source
{
public:
    [[nodiscard]] nucleus::capability_descriptor capabilities() const override
    {
        return {nucleus::capability::nesting};
    }

    [[nodiscard]] nucleus::configuration_source_result pull() override
    {
        nucleus::configuration_source_batch batch;
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
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    // A custom source that satisfies the source concept is moved directly into a
    // source_handle and layered through the explicit stack.
    auto loaded = nucleus::load(space,
        nucleus::source_stack{nucleus::source_handle(table_source{})},
        {});
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
