#ifndef HPP_GUARD_NUCLEUS_SOURCE_PARSER_H
#define HPP_GUARD_NUCLEUS_SOURCE_PARSER_H

#include "nucleus/source/source.h"
#include "nucleus/capability.h"

#include <concepts>

namespace nucleus {

// The authoring surface for a source. A Parser is any struct that declares its
// capabilities and produces a source_batch -- no inheritance, no virtuals. This
// is the "concept to author" half of the seam: an author writes a plain struct
// satisfying these two operations, and parser_adapter<T> type-erases it into the
// runtime-virtual `source` for registration ("virtual to inject"). The same
// abstract pull path then serves both a hand-written virtual source and any
// concept-satisfying struct -- which is what proves the seam is real rather than
// a single-format stub.
template <typename T>
concept Parser = requires(T parser) {
    { parser.capabilities() } -> std::convertible_to<capability_descriptor>;
    { parser.pull() } -> std::convertible_to<source_result>;
};

}

#endif
