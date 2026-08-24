// custom_source: implement the source seam with a plain struct -- no inheritance.
//
// A source declares its capabilities() and yields entries from pull(). Any struct
// satisfying those two members is a source: it flows through source_handle and into
// the engine the same way argv, env, or xml does. This one emits owned values, so
// it pins no buffer (retained_buffer::none()).

#include "nucleus/config_space.h"

#include "nucleus/capability.h"

#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/config_source.h"
#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <iostream>

namespace {

// A source backed by an in-memory table. No base class, no virtuals -- just the
// two members the concept requires. It owns its values, so views never outlive a
// backing buffer and no retained_buffer is needed.
struct table_source
{
    nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::nesting};
    }

    nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
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
    const auto sealed = nucleus::config_space_builder{}.build();
    if(!sealed)
        return 1;
    const nucleus::config_space &space = sealed.value();

    // A custom source that satisfies the source concept is moved directly into a
    // source_handle and layered through the explicit stack.
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{nucleus::source_handle(table_source{})},
        {});
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::config &config = loaded.value();
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';
    return 0;
}
