// source_concept: satisfy the source concept with a plain struct -- no inheritance.
//
// Any struct with capabilities() and pull() satisfies nucleus::config_source.
// source_handle type-erases it into the engine so a plain-struct source reaches
// the fold through the same erasure path any other source does.

#include "nucleus/capability.h"

#include "nucleus/config_source/source_concept.h"
#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/config_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <iostream>

namespace {

// No base class, no virtuals -- just the two members the concept requires.
struct table_parser
{
    nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::ordering};
    }

    nucleus::config_source_result pull()
    {
        nucleus::config_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "x/y", nucleus::value::owned("from-parser"), capabilities()));
        return batch;
    }
};

static_assert(nucleus::config_source<table_parser>,
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
