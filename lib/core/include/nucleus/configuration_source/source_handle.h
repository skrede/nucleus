#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_SOURCE_HANDLE_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_SOURCE_HANDLE_H

#include "nucleus/configuration_source/source_concept.h"
#include "nucleus/configuration_source/configuration_source.h"

#include <memory>
#include <utility>

namespace nucleus {

// Move-only value type that erases a concept-satisfying source behind a manual vtable.
// Erasure hides (does not remove) the vtable dispatch; the dispatch cost equals today's virtual.
class source_handle
{
public:
    // Constructs a handle owning a moved-in source. Constrained to concept-satisfying types.
    template <configuration_source S>
    explicit source_handle(S s)
        : m_self(std::make_unique<model<S>>(std::move(s)))
    {
    }

    source_handle(source_handle &&) noexcept            = default;
    source_handle & operator=(source_handle &&) noexcept = default;

    source_handle(const source_handle &)            = delete;
    source_handle & operator=(const source_handle &) = delete;

    [[nodiscard]] capability_descriptor capabilities() const { return m_self->do_caps(); }

    void apply_projection(const schema_projection & p) { m_self->do_project(p); }

    [[nodiscard]] inherit_declaration inheritance() const { return m_self->do_inherit(); }

    [[nodiscard]] configuration_source_result pull() { return m_self->do_pull(); }

private:
    struct concept_t
    {
        virtual ~concept_t()                                     = default;
        virtual capability_descriptor    do_caps() const         = 0;
        virtual void                     do_project(const schema_projection &) = 0;
        virtual inherit_declaration      do_inherit() const      = 0;
        virtual configuration_source_result do_pull()            = 0;
    };

    template <class S>
    struct model final : concept_t
    {
        explicit model(S s) : m_src(std::move(s)) {}

        capability_descriptor do_caps() const override { return m_src.capabilities(); }

        void do_project(const schema_projection & p) override
        {
            if constexpr (projects_source<S>)
                m_src.apply_projection(p);
            // else no-op: flat sources ignore projections
        }

        inherit_declaration do_inherit() const override
        {
            if constexpr (inheriting_source<S>)
                return m_src.inheritance();
            else
                return {};
        }

        configuration_source_result do_pull() override { return m_src.pull(); }

        S m_src;
    };

    std::unique_ptr<concept_t> m_self;
};

}

#endif
