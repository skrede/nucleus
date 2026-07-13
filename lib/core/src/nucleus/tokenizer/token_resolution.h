#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TOKEN_RESOLUTION_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TOKEN_RESOLUTION_H

#include "nucleus/expected.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/resolve_error.h"
#include "nucleus/tokenizer/resolver_scope.h"
#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/tokenizer/substitution_budget.h"
#include "nucleus/tokenizer/tree_tokenizer_registry.h"

#include <utility>
#include <filesystem>
#include <string_view>

namespace nucleus {

// Resolution entry point for the simple, frame-less case: expand every ${...} in
// `value` against the registered tokenizers, with no lexical scope. Suitable for
// values whose source has no file location and no host frames.
inline token_result resolve_tokens(std::string_view value,
                                   const tokenizer_registry &registry,
                                   const tree_tokenizer_registry *tree_reg = nullptr)
{
    resolver_scope scope(registry, default_expansion_depth_cap, tree_reg);
    return scope.resolve_all(value);
}

// Resolution entry point for the common expand-then-layer case: expand every
// ${...} in `value` against the registered tokenizers AND a file frame carrying
// the source location, so ${scope.file_*} resolve. The convergence layer calls
// this per value at source-read time, before layering merges already-resolved
// values. For richer lexical scopes (host frame categories, function param
// frames) construct a resolver_scope directly and push frames before
// resolve_all.
inline token_result resolve_tokens(std::string_view value,
                                   const tokenizer_registry &registry,
                                   std::filesystem::path source_location,
                                   const tree_tokenizer_registry *tree_reg = nullptr)
{
    resolver_scope scope(registry, default_expansion_depth_cap, tree_reg);
    auto frame = scope.push_file_frame(std::move(source_location));
    return scope.resolve_all(value);
}

// Per-load entry points that borrow a shared substitution budget, so the count
// is charged across every value in one fold pass rather than reset per value.
inline token_result resolve_tokens(std::string_view value,
                                   const tokenizer_registry &registry,
                                   substitution_budget &budget,
                                   const tree_tokenizer_registry *tree_reg = nullptr)
{
    resolver_scope scope(registry, budget, default_expansion_depth_cap, tree_reg);
    return scope.resolve_all(value);
}

inline token_result resolve_tokens(std::string_view value,
                                   const tokenizer_registry &registry,
                                   std::filesystem::path source_location,
                                   substitution_budget &budget,
                                   const tree_tokenizer_registry *tree_reg = nullptr)
{
    resolver_scope scope(registry, budget, default_expansion_depth_cap, tree_reg);
    auto frame = scope.push_file_frame(std::move(source_location));
    return scope.resolve_all(value);
}

}

#endif
