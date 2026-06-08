// parser_concept: satisfy the Parser concept with a plain struct -- no inheritance.
//
// A type with capabilities() and pull() satisfies nucleus::Parser. adapt_parser
// type-erases it into a `source`, so a value-type parser reaches the engine
// through the SAME virtual path a hand-written subclass does.

#include "nucleus/capability.h"

#include "nucleus/configuration_source/parser.h"
#include "nucleus/configuration_source/configuration_source.h"
#include "nucleus/configuration_source/parser_adapter.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <memory>
#include <iostream>

namespace {

// No base class, no virtuals -- just the two members the concept requires.
struct table_parser
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const
    {
        return {nucleus::capability::ordering};
    }

    [[nodiscard]] nucleus::configuration_source_result pull() const
    {
        nucleus::configuration_source_batch batch;
        batch.entries.push_back(nucleus::make_entry(
            "x/y", nucleus::value::owned("from-parser"), capabilities()));
        return batch;
    }
};

static_assert(nucleus::Parser<table_parser>,
              "table_parser must satisfy the Parser concept");

}

int main()
{
    std::unique_ptr<nucleus::configuration_source> source = nucleus::adapt_parser(table_parser{});

    auto pulled = source->pull();
    if(!pulled)
    {
        std::cerr << "pull failed: " << pulled.error() << '\n';
        return 1;
    }

    for(const nucleus::keyspace_entry &entry : pulled.value().entries)
        std::cout << entry.path << " = " << entry.value.text() << '\n';
    return 0;
}
