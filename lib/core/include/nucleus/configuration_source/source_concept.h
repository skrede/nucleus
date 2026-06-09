#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_SOURCE_CONCEPT_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_SOURCE_CONCEPT_H

#include "nucleus/configuration_source/configuration_source.h"

#include <concepts>

namespace nucleus {

// The compile-time contract a source must satisfy: declare capabilities and produce a batch.
// Staged name -- will become `configuration_source` once the virtual base class is removed.
//
// Buffer-lifetime rule for document-backed sources: if pull() returns entries whose values are
// VIEWS into a parser-owned arena, the batch MUST carry ownership of that arena in its
// retained_buffer. Resolution copies values out and drops the batch only then; a source
// that returns views without pinning the arena would dangle the instant the batch is released.
template <typename S>
concept source_satisfies =
    requires(S s) {
        { s.capabilities() } -> std::convertible_to<capability_descriptor>;
        { s.pull() }         -> std::convertible_to<configuration_source_result>;
    };

// Detected (never required): a source that accepts a schema-derived projection before pull().
template <typename S>
concept projects_source =
    source_satisfies<S> &&
    requires(S s, const schema_projection & p) { s.apply_projection(p); };

// Detected (never required): a source that declares an inheritance chain parent after pull().
template <typename S>
concept inheriting_source =
    source_satisfies<S> &&
    requires(const S s) {
        { s.inheritance() } -> std::convertible_to<inherit_declaration>;
    };

}

#endif
