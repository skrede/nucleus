#ifndef HPP_GUARD_NUCLEUS_ENTRY_PRECEDENCE_H
#define HPP_GUARD_NUCLEUS_ENTRY_PRECEDENCE_H

#include "nucleus/identity.h"

#include "nucleus/source/source.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// The canonical precedence ranks for the v0.1 layering, lowest -> highest. A
// higher rank wins a key contest. The names describe ROLES, not formats: a
// document source can be a base or an overlay; argv and env are flat sources. The
// order is the locked one -- argv overrides everything, defaults are the floor:
//
//   defaults < env < base < overlay < argv
//
// The document band (base..overlay-1) spans 700 slots (200..899), supporting
// inheritance chains up to length 700 before clamping. A host that needs finer
// control sets an explicit numeric rank per layer; these are just the anchors.
enum class layer_rank : std::size_t
{
    defaults = 0,
    env      = 100,
    base     = 200,
    overlay  = 900,
    argv     = 1000,
};

// One layer of the source stack: a source to pull, the precedence rank that
// decides contests, and a host-readable label that travels into provenance. The
// source is BORROWED (a non-owning pointer) -- the caller owns it for the duration
// of the load; the stack never takes ownership.
struct source_layer
{
    source *src = nullptr;
    std::size_t rank = 0;
    std::string label;
    // The opaque owner token of the source, surfaced into provenance for "who set
    // this?" -- carried, never interpreted.
    owner_token owner;
};

// An ordered set of source layers with explicit precedence -- the argument
// load()/resolve() folds. It is just a list of borrowed sources plus their ranks;
// the same fold runs whether there is one layer or five (no per-format branch).
class source_stack
{
public:
    source_stack() = default;

    // Adds a layer at an explicit numeric rank.
    source_stack &add(source &src, std::size_t rank, std::string label, owner_token owner = {})
    {
        m_layers.push_back(source_layer{&src, rank, std::move(label), std::move(owner)});
        return *this;
    }

    // Adds a layer at one of the well-known ranks.
    source_stack &add(source &src, layer_rank rank, std::string label, owner_token owner = {})
    {
        return add(src, static_cast<std::size_t>(rank), std::move(label), std::move(owner));
    }

    [[nodiscard]] const std::vector<source_layer> &layers() const noexcept
    {
        return m_layers;
    }

    [[nodiscard]] bool empty() const noexcept { return m_layers.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_layers.size(); }

private:
    std::vector<source_layer> m_layers;
};

}

#endif
