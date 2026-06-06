#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_RESOLVER_SCOPE_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_RESOLVER_SCOPE_H

#include "nucleus/result.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/scope_frame.h"
#include "nucleus/tokenizer/resolve_error.h"
#include "nucleus/tokenizer/expansion_guard.h"

#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <filesystem>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace nucleus {

class tokenizer_registry;

// Move-only RAII frame popper. push_*_frame returns one; it pops its frame on
// destruction (including stack unwind), so a frame's lifetime is scoped to the
// resolution it brackets and never leaks across calls.
class frame_guard
{
public:
    frame_guard() = default;
    explicit frame_guard(std::function<void()> pop) : m_pop(std::move(pop)) {}

    frame_guard(const frame_guard &) = delete;
    frame_guard &operator=(const frame_guard &) = delete;

    frame_guard(frame_guard &&other) noexcept : m_pop(std::move(other.m_pop)) { other.m_pop = nullptr; }
    frame_guard &operator=(frame_guard &&other) noexcept
    {
        if(this != &other)
        {
            if(m_pop) m_pop();
            m_pop = std::move(other.m_pop);
            other.m_pop = nullptr;
        }
        return *this;
    }

    ~frame_guard() { if(m_pop) m_pop(); }

private:
    std::function<void()> m_pop;
};

// The token-resolution engine for one value (or one logical resolution pass over
// several values sharing a scope). It owns the innermost-first frame stack and
// the expansion guard, and BORROWS the tokenizer registry -- the only sibling it
// ever touches, passed in by the caller and never stored as anything but the
// borrowed reference for this transient resolution.
//
// Dispatch order for ${category.name}:
//   1. the reserved "scope" category -> the core file-frame keys.
//   2. a host-registered generic frame category on the stack -> its bindings.
//   3. the tokenizer registry -> a registered tokenizer's field/function.
class resolver_scope
{
public:
    explicit resolver_scope(const tokenizer_registry &registry,
                            std::size_t depth_cap = default_expansion_depth_cap)
        : m_registry(registry), m_guard(depth_cap)
    {
    }

    resolver_scope(const resolver_scope &) = delete;
    resolver_scope &operator=(const resolver_scope &) = delete;

    // Pushes the generic core file frame carrying the value's source location.
    // Activates the ${scope.file_*} keys for the bracketed resolution.
    [[nodiscard]] frame_guard push_file_frame(std::filesystem::path file);

    // Pushes a host-named generic frame category whose bindings answer
    // ${category.<name>} directly. THIS is the mechanism a host uses to register
    // additional, vocabulary-specific scope frame categories without the core
    // ever learning the vocabulary. Innermost-first: a later push of the same
    // category shadows an earlier one.
    [[nodiscard]] frame_guard push_scope_frame(std::string category,
                                               std::unordered_map<std::string, std::string> bindings);

    // Pushes a param frame carrying function-invocation arguments keyed by
    // parameter name, reached through the reserved "args" category.
    [[nodiscard]] frame_guard push_param_frame(std::unordered_map<std::string, std::string> params);

    // Resolves every ${...} in `input` recursively to a fixpoint and splices the
    // results back into the surrounding text. Returns the fully expanded string,
    // or the first resolution error encountered (a malformed token, an unknown
    // reference, or a cycle/depth halt).
    [[nodiscard]] token_result resolve_all(std::string_view input);

    // Resolves one whole ${...} token (the input must be a single token).
    [[nodiscard]] token_result resolve_one(std::string_view token);

private:
    const tokenizer_registry &m_registry;
    expansion_guard m_guard;
    std::vector<scope_frame> m_frames;

    void pop_frame() noexcept;

    [[nodiscard]] token_result dispatch_field(std::string_view category, std::string_view name);
    [[nodiscard]] token_result dispatch_function(std::string_view category, std::string_view name,
                                                 std::span<const std::string> args);
    [[nodiscard]] token_result lookup_frame_binding(std::string_view category,
                                                    std::string_view name) const;
};

}

#endif
