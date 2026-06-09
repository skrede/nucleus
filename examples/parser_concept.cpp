// source_concept: satisfy the source concept with a plain struct -- no inheritance.
//
// Any struct with capabilities() and pull() satisfies nucleus::configuration_source.
// source_handle type-erases it into the engine so a plain-struct source reaches
// the fold through the same erasure path any other source does.

#include "nucleus/capability.h"

#include "nucleus/configuration_source/source_concept.h"
#include "nucleus/configuration_source/source_handle.h"
#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <iostream>

namespace {

// No base class, no virtuals -- just the two members the concept requires.
struct table_parser
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::ordering};
    }

    [[nodiscard]] nucleus::configuration_source_result pull()
    {
        nucleus::configuration_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "x/y", nucleus::value::owned("from-parser"), capabilities()));
        return batch;
    }
};

static_assert(nucleus::configuration_source<table_parser>,
              "table_parser must satisfy the source concept");

}

int main()
{
    nucleus::source_handle source{table_parser{}};

    auto pulled = source.pull();
    if(!pulled)
    {
        std::cerr << "pull failed: " << pulled.error() << '\n';
        return 1;
    }

    for(const nucleus::keyspace_entry &entry : pulled.value().entries)
        std::cout << entry.path << " = " << entry.value.text() << '\n';
    return 0;
}
