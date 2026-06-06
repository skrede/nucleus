#ifndef HPP_GUARD_NUCLEUS_ENTRY_RESOLUTION_CONTEXT_H
#define HPP_GUARD_NUCLEUS_ENTRY_RESOLUTION_CONTEXT_H

#include "nucleus/schema/schema_registry.h"
#include "nucleus/source/source_registry.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

namespace nucleus {

// The transient hand-off vehicle. It BORROWS (holds references to) the three
// flat sibling registries the facade owns; it does not own them and lives only
// for the duration of one load()/resolve(). This is the ONLY path by which one
// registry reaches another: a registry operation takes the context (or a
// specific sibling) as a parameter and never stores it. Living in entry/ -- the
// one place that knows all three registries by type -- physically reinforces
// that no registry owns another.
//
// In this phase the context is the minimal borrowing shell; later phases flesh
// it out with the building keyspace, provenance, diagnostics, and retained
// source buffers.
class resolution_context
{
public:
    resolution_context(schema_registry &schema,
                        tokenizer_registry &tokenizer,
                        source_registry &sources) noexcept
        : m_schema(schema), m_tokenizer(tokenizer), m_sources(sources)
    {
    }

    [[nodiscard]] schema_registry &schema() noexcept { return m_schema; }
    [[nodiscard]] tokenizer_registry &tokenizer() noexcept { return m_tokenizer; }
    [[nodiscard]] source_registry &sources() noexcept { return m_sources; }

private:
    schema_registry &m_schema;
    tokenizer_registry &m_tokenizer;
    source_registry &m_sources;
};

}

#endif
