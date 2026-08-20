#ifndef HPP_GUARD_NUCLEUS_RESOLVE_REFERENCE_RESOLUTION_H
#define HPP_GUARD_NUCLEUS_RESOLVE_REFERENCE_RESOLUTION_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/tokenizer/resolve_error.h"
#include "nucleus/tokenizer/expansion_guard.h"
#include "nucleus/tokenizer/tree_resolver_scope.h"
#include "nucleus/tokenizer/substitution_budget.h"
#include "nucleus/tokenizer/tree_tokenizer_registry.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace nucleus {

// Pass-2 tree-reference resolution. It BORROWS the building keyspace it reads
// and writes back into, and the tree tokenizer registry a token's category may
// name; it keeps no state between passes -- the guard, the cache and the budget
// all live for the duration of one resolve() call.
class reference_resolution
{
public:
    reference_resolution(keyspace &building,
                         const tree_tokenizer_registry &tree_tokenizer) noexcept
        : m_building(building)
        , m_tree_tokenizer(tree_tokenizer)
    {
    }

    // Resolves all ${abs:} and ${rel:} tokens in the sliced keyspace, writing the
    // resolved strings back to the same paths. Enforces:
    //   - Value-only invariant: a key segment containing "${" is a loud error.
    //   - Cross-leaf cycle detection via expansion_guard.
    //   - Substitution-count budget: budget_exceeded stops billion-laughs.
    //   - ?? chaining: missing_field falls through; all other errors propagate.
    expected<void, resolve_fold_error> resolve(std::size_t reference_budget)
    {
        if(auto scanned = scan_structural_keys(); !scanned)
            return scanned;

        expansion_guard leaf_guard(default_reference_depth_cap);
        std::unordered_map<std::string, std::string> resolved_cache;
        substitution_budget budget(reference_budget);

        // Snapshot the paths: resolving writes back through m_building.set().
        const std::vector<key_path> all_paths = m_building.paths();
        for(const key_path &kp : all_paths)
            if(auto one = resolve_leaf(kp, leaf_guard, resolved_cache, budget); !one)
                return one;
        return {};
    }

private:
    // The tree shape is frozen by slice(), so a reference may only appear in a
    // value; one in a key segment would make the tree untraversable.
    expected<void, resolve_fold_error> scan_structural_keys() const
    {
        for(const key_path &kp : m_building.paths())
            for(const std::string &seg : kp.segments())
                if(seg.find("${") != std::string::npos)
                    return unexpected(error{errc::unresolved_token,
                        nucleus::format("reference in structural key position: '{}'",
                                       kp.str())});
        return {};
    }

    // The pass's per-leaf step: skips a leaf carrying no token, and lifts the
    // recursive resolver's resolve_error into the error the stage reports.
    expected<void, resolve_fold_error>
    resolve_leaf(const key_path &kp, expansion_guard &leaf_guard,
                 std::unordered_map<std::string, std::string> &resolved_cache,
                 substitution_budget &budget)
    {
        const value *v = m_building.find(kp);
        if(v == nullptr || v->text().find("${") == std::string_view::npos)
            return {};
        auto one = resolve_one_leaf(kp, leaf_guard, resolved_cache, budget);
        if(one)
            return {};
        return unexpected(error{errc::unresolved_token,
            nucleus::format("reference resolution failed for '{}': {}",
                           kp.str(), one.error().message)});
    }

    // Recursive single-leaf resolver: resolves `kp`'s value by first ensuring
    // every leaf it references is itself resolved (depth-first). Returns a
    // resolve_error rather than a resolve_fold_error so it composes with the
    // ensure_resolved_fn callback type tree_resolver_scope takes.
    expected<void, resolve_error>
    resolve_one_leaf(const key_path &kp, expansion_guard &leaf_guard,
                     std::unordered_map<std::string, std::string> &resolved_cache,
                     substitution_budget &budget)
    {
        const std::string path_str = kp.str();
        const std::optional<std::string_view> text =
            pending_text(kp, path_str, resolved_cache);
        if(!text.has_value())
            return {};
        // Enter the cross-leaf guard -- it detects A -> B -> A cycles, and must
        // stay entered for the whole depth-first expansion below.
        auto guard_scope = leaf_guard.enter(path_str);
        if(!guard_scope)
            return unexpected(std::move(guard_scope).error());
        return expand_leaf(kp, text.value(), leaf_guard, resolved_cache, budget);
    }

    // The value awaiting expansion at `kp`, or nothing when the leaf is already
    // resolved in this pass, is absent, or carries no token. After the pass-1
    // fold any remaining ${ is a tree-access token pass-1 left verbatim, so a
    // leaf without one is final and is cached as it stands.
    std::optional<std::string_view>
    pending_text(const key_path &kp, const std::string &path_str,
                 std::unordered_map<std::string, std::string> &resolved_cache) const
    {
        if(resolved_cache.contains(path_str))
            return std::nullopt;
        const value *v = m_building.find(kp);
        if(v == nullptr)
            return std::nullopt;
        const std::string_view text = v->text();
        if(text.find("${") != std::string_view::npos)
            return text;
        resolved_cache[path_str] = std::string(text);
        return std::nullopt;
    }

    expected<void, resolve_error>
    expand_leaf(const key_path &kp, std::string_view text,
                expansion_guard &leaf_guard,
                std::unordered_map<std::string, std::string> &resolved_cache,
                substitution_budget &budget)
    {
        ensure_resolved_fn ensure = [&](const key_path &target)
            -> expected<void, resolve_error>
        {
            return resolve_one_leaf(target, leaf_guard, resolved_cache, budget);
        };
        tree_resolver_scope scope(m_building, kp, budget,
                                  std::move(ensure), &m_tree_tokenizer);
        auto resolved = scope.resolve_value(text);
        if(!resolved)
            return unexpected(std::move(resolved).error());
        resolved_cache[kp.str()] = resolved.value();
        m_building.set(kp, value::owned(std::move(resolved).value()));
        return {};
    }

    keyspace                      &m_building;
    const tree_tokenizer_registry &m_tree_tokenizer;
};

}

#endif
