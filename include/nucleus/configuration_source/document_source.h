#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_DOCUMENT_SOURCE_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_DOCUMENT_SOURCE_H

#include "nucleus/configuration_source/configuration_source.h"

namespace nucleus {

// The document-source subcategory. A document source parses an input into a tree
// and walks it into keyspace entries whose values are VIEWS into the parser's
// retained arena -- the view-node model. It is a subcategory of `source`, not a
// new boundary: the boundary is `source`. env and argv are sources too; they are
// simply not documents and do not share this model.
//
// The load-bearing addition over the bare source contract is the buffer-lifetime
// rule, stated here as the type's documented invariant: every batch a document
// source returns MUST carry, in its retained_buffer, ownership of the arena its
// view-values point into (and, transitively, of every other document those views
// reach). Resolution copies values out and only then drops the batch; until then
// the arena is pinned. A document source that returned views without pinning the
// arena would dangle the instant the batch outlived the parser -- the project's
// top use-after-free risk. This intermediate base exists so that contract has a
// single, named home that every concrete document source inherits, keeping the
// rule auditable in one place. (Concrete document sources live in their own
// quarantined parser modules; the core only ever sees this base.)
class document_source : public configuration_source
{
public:
    // Document sources are inherently tree-structured, so the structural baseline
    // is at least nesting. Concrete sources widen this with the affordances their
    // format actually preserves (duplicate keys, ordering, comments, ...).
    [[nodiscard]] capability_descriptor capabilities() const override = 0;

    [[nodiscard]] configuration_source_result pull() override = 0;
};

}

#endif
