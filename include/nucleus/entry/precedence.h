#ifndef HPP_GUARD_NUCLEUS_ENTRY_PRECEDENCE_H
#define HPP_GUARD_NUCLEUS_ENTRY_PRECEDENCE_H

#include "nucleus/identity.h"

#include "nucleus/configuration_source/configuration_source.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// Canonical precedence ranks, lowest -> highest (a higher rank wins a key contest);
// the names describe roles, not formats. Locked order:
//   defaults < env < base < overlay < argv
// The document band (200..899) spans 700 slots for inheritance chains; a host may
// set an explicit numeric rank per layer instead of these anchors.
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
struct configuration_source_layer
{
    configuration_source *src = nullptr;
    std::size_t rank = 0;
    std::string label;
    // The opaque owner token of the source, surfaced into provenance for "who set
    // this?" -- carried, never interpreted.
    owner_token owner;
};

// An ordered set of layers with explicit precedence -- the argument
// load_configuration() folds. A list of borrowed sources plus ranks; the same fold
// runs for one layer or five (no per-format branch).
class configuration_source_stack
{
public:
    configuration_source_stack() = default;

    // Adds a layer at an explicit numeric rank.
    configuration_source_stack &add(configuration_source &src, std::size_t rank, std::string label, owner_token owner = {})
    {
        m_layers.push_back(configuration_source_layer{&src, rank, std::move(label), std::move(owner)});
        return *this;
    }

    // Adds a layer at one of the well-known ranks.
    configuration_source_stack &add(configuration_source &src, layer_rank rank, std::string label, owner_token owner = {})
    {
        return add(src, static_cast<std::size_t>(rank), std::move(label), std::move(owner));
    }

    [[nodiscard]] const std::vector<configuration_source_layer> &layers() const noexcept
    {
        return m_layers;
    }

    [[nodiscard]] bool empty() const noexcept { return m_layers.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_layers.size(); }

private:
    std::vector<configuration_source_layer> m_layers;
};

}

#endif
