#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TOKEN_RESOLUTION_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TOKEN_RESOLUTION_H

#include "nucleus/result.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/resolve_error.h"
#include "nucleus/tokenizer/resolver_scope.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <utility>
#include <filesystem>
#include <string_view>

namespace nucleus {

// Resolution entry point for the simple, frame-less case: expand every ${...} in
// `value` against the registered tokenizers, with no lexical scope. Suitable for
// values whose source has no file location and no host frames.
[[nodiscard]] inline token_result resolve_tokens(std::string_view value,
                                                 const tokenizer_registry &registry)
{
    resolver_scope scope(registry);
    return scope.resolve_all(value);
}

// Resolution entry point for the common expand-then-layer case: expand every
// ${...} in `value` against the registered tokenizers AND a file frame carrying
// the source location, so ${scope.file_*} resolve. The convergence layer calls
// this per value at source-read time, before layering merges already-resolved
// values. For richer lexical scopes (host frame categories, function param
// frames) construct a resolver_scope directly and push frames before
// resolve_all.
[[nodiscard]] inline token_result resolve_tokens(std::string_view value,
                                                 const tokenizer_registry &registry,
                                                 std::filesystem::path source_location)
{
    resolver_scope scope(registry);
    auto frame = scope.push_file_frame(std::move(source_location));
    return scope.resolve_all(value);
}

}

#endif
