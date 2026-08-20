#ifndef HPP_GUARD_NUCLEUS_RESOLVE_LAYER_FOLD_H
#define HPP_GUARD_NUCLEUS_RESOLVE_LAYER_FOLD_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/schema/projection.h"
#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/config_source/config_source.h"
#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/inherit_declaration.h"

#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <functional>

namespace nucleus {

// Owns the fold's layer axis: the rank order layers are consumed in, the schema
// lookups every layer shares, the batch a layer contributes, and the bookkeeping
// that is scoped to one layer. The schema and the dispositions and buffers that
// outlive the fold are borrowed; the pulled batch is held here so it outlives the
// entry loop reading it.
class layer_fold
{
    // Scoped to ONE layer: a fresh layer re-mints leaf ordinals and sweeps every
    // instance again, so carrying any of this across a layer boundary would let a
    // later layer skip the sweep by which it displaces an earlier one.
    struct layer_state
    {
        std::map<std::string, std::size_t> leaf_ordinals;
        std::set<std::string>              swept;
        std::map<std::string, std::map<std::string, std::vector<key_path>>> buckets;
    };

public:
    layer_fold(const schema_registry &schema, std::vector<retained_buffer> &buffers,
               std::vector<extend_disposition> &dispositions) noexcept
        : m_schema(schema)
        , m_buffers(buffers)
        , m_dispositions(dispositions)
    {
    }

    // Refreshes the lookups shared across the whole fold and returns the layers in
    // the ascending rank order the fold consumes them in.
    std::vector<layered_handle *> begin_fold(std::span<layered_handle> layers)
    {
        m_projection = m_schema.projection();
        m_repeated_declared = repeated_declared_paths(m_schema);
        m_repeated_containers = m_schema.repeated_container_paths();
        return rank_order(layers);
    }

    // A chain layer's batch was already pulled during the inheritance walk, so it
    // is consumed here rather than pulling the same handle a second time. Stack,
    // argv and env layers have no walk phase and are pulled here for the only time.
    expected<std::reference_wrapper<config_source_batch>, resolve_fold_error>
    acquire(layered_handle &layer)
    {
        if(layer.cached_batch != nullptr)
            return std::ref(*layer.cached_batch);
        layer.handle->apply_projection(m_projection);
        m_pulled = layer.handle->pull();
        if(!m_pulled)
            return unexpected(error{m_pulled.error().code, nucleus::format(
                "source '{}': {}", layer.label, m_pulled.error().message)});
        return std::ref(m_pulled.value());
    }

    expected<void, resolve_fold_error> begin_layer(const config_source_batch &batch)
    {
        m_state = {};
        return reject_extend_on_repeated(batch);
    }

    void end_layer(config_source_batch &batch)
    {
        for(const extend_disposition &d : batch.dispositions)
            m_dispositions.push_back(d);
        m_buffers.push_back(std::move(batch.buffer));
    }

    const std::set<std::string> &repeated_declared() const noexcept
    {
        return m_repeated_declared;
    }

    const std::set<std::string> &repeated_containers() const noexcept
    {
        return m_repeated_containers;
    }

    layer_state &state() noexcept { return m_state; }

private:
    const schema_registry           &m_schema;
    std::vector<retained_buffer>    &m_buffers;
    std::vector<extend_disposition> &m_dispositions;
    layer_state             m_state;
    schema_projection       m_projection;
    config_source_result    m_pulled;
    std::set<std::string>   m_repeated_declared;
    std::set<std::string>   m_repeated_containers;

    static std::vector<layered_handle *> rank_order(std::span<layered_handle> layers)
    {
        std::vector<layered_handle *> ordered;
        ordered.reserve(layers.size());
        for(layered_handle &lh : layers)
            ordered.push_back(&lh);
        // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order): ordered by the stable rank key, not by pointer address, so the result is deterministic.
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const layered_handle *a, const layered_handle *b) {
                             return a->rank < b->rank;
                         });
        return ordered;
    }

    expected<void, resolve_fold_error>
    reject_extend_on_repeated(const config_source_batch &batch) const
    {
        for(const extend_disposition &d : batch.dispositions)
            if(m_repeated_containers.contains(d.container_path))
                return unexpected(error{errc::layering_violation,
                    nucleus::format(
                        "extend= targeting repeated container '{}' is not "
                        "supported: a repeated container composes across layers "
                        "by ordinal instance, not by extension",
                        d.container_path)});
        return {};
    }
};

}

#endif
