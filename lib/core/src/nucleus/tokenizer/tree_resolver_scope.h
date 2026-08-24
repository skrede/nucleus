#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TREE_RESOLVER_SCOPE_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TREE_RESOLVER_SCOPE_H

#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/tokenizer/expansion_guard.h"
#include "nucleus/tokenizer/substitution_budget.h"
#include "nucleus/tokenizer/resolve_error.h"
#include "nucleus/tokenizer/token_lexer.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/tree_tokenizer_registry.h"

#include <functional>
#include <string>
#include <string_view>

namespace nucleus {

// Callback type used to ensure a referenced leaf is resolved before its value
// is read. Called by tree_resolver_scope for each abs:/rel: target path.
// Returns an error if the target's resolution fails (e.g. cycle or budget).
using ensure_resolved_fn =
    std::function<expected<void, resolve_error>(const key_path &)>;

// Pass-2 resolver for tree-access tokens (${abs:...} and ${rel:...}) in a
// single value string. Constructed transiently per leaf during resolve_references();
// borrows (never stores) the building keyspace, the shared substitution counter
// and the load-wide dispatch chain. Cross-leaf cycle detection is owned by the
// caller (resolve_one_leaf enters/exits the guard around each leaf); the
// recursive ensure_resolved callback routes back through it.
// Flat-registry purity: no stored cross-registry member; tree_resolver_scope
// lives only for the duration of one leaf resolution.
class tree_resolver_scope
{
public:
    tree_resolver_scope(const keyspace &building,
                        key_path current_path,
                        substitution_budget &budget,
                        expansion_guard &dispatch_guard,
                        ensure_resolved_fn ensure_resolved,
                        const tree_tokenizer_registry *tree_reg = nullptr) noexcept;

    // Resolves all ${abs:} and ${rel:} tokens in value_text, splicing resolved
    // strings in place. Returns the fully-resolved string on success.
    token_result resolve_value(std::string_view value_text);

    // Resolves one fallback arm (the text between '??' separators, already trimmed).
    // A fallback arm may be a bare literal; the first arm of a token may not.
    token_result resolve_one_arm(std::string_view arm, bool is_fallback);

private:
    token_result expand_produced(token_result produced);
    token_result resolve_absolute(std::string_view path_body);
    token_result resolve_relative(std::string_view rel_body);
    expected<key_path, resolve_error> resolve_relative_path(std::string_view rel_body);

    const keyspace                  &m_building;
    key_path                         m_current_path;
    substitution_budget             &m_budget;
    ensure_resolved_fn               m_ensure_resolved;
    const tree_tokenizer_registry   *m_tree_tokenizer = nullptr;
    // Borrowed, never owned: one chain spans the whole depth-first expansion, so
    // constructing a scope per leaf cannot reset the dispatch nesting.
    expansion_guard                 &m_dispatch_guard;
};

}

#endif
