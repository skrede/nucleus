#include "nucleus/tokenizer/tree_resolver_scope.h"

#include "nucleus/format.h"

#include <string>
#include <utility>
#include <cstddef>
#include <string_view>

namespace nucleus {

tree_resolver_scope::tree_resolver_scope(const keyspace &building,
                                         key_path current_path,
                                         expansion_guard &leaf_guard,
                                         std::size_t &substitution_counter,
                                         std::size_t budget,
                                         ensure_resolved_fn ensure_resolved,
                                         const tree_tokenizer_registry *tree_reg) noexcept
    : m_building(building)
    , m_current_path(std::move(current_path))
    , m_leaf_guard(leaf_guard)
    , m_substitution_counter(substitution_counter)
    , m_budget(budget)
    , m_ensure_resolved(std::move(ensure_resolved))
    , m_tree_tokenizer(tree_reg)
{
}

// Scans value_text for ${...} tokens and splices each resolved value in place.
// Nested ${} depths are tracked so braces inside a token body are not mistaken
// for the closing brace of the outer token.
token_result tree_resolver_scope::resolve_value(std::string_view value_text)
{
    if(value_text.find("${") == std::string_view::npos)
        return std::string(value_text);

    std::string result;
    result.reserve(value_text.size());

    std::size_t pos = 0;
    while(pos < value_text.size())
    {
        auto open = value_text.find("${", pos);
        if(open == std::string_view::npos)
        {
            result.append(value_text.substr(pos));
            break;
        }

        // Literal text before the token.
        result.append(value_text.substr(pos, open - pos));

        // Find matching closing brace, tracking nested ${} depth.
        std::size_t depth = 1;
        std::size_t i = open + 2;
        while(i < value_text.size() && depth > 0)
        {
            if(value_text[i] == '$' && i + 1 < value_text.size()
               && value_text[i + 1] == '{')
            {
                ++depth;
                i += 2;
            }
            else if(value_text[i] == '}')
            {
                --depth;
                ++i;
            }
            else
                ++i;
        }
        if(depth != 0)
            return unexpected(resolve_error(resolve_errc::parse_error,
                                      "unterminated ${ in value"));

        // The full token body (between ${ and the matching }).
        const std::string_view token_body = value_text.substr(open + 2, i - open - 3);
        pos = i;

        // Split on top-level '??' to get fallback arms.
        std::vector<std::string_view> arms = split_fallback_arms(token_body);

        token_result last = unexpected(resolve_error(
            resolve_errc::missing_field, "all fallback arms absent"));
        bool succeeded = false;
        for(std::string_view arm : arms)
        {
            auto r = resolve_one_arm(arm);
            if(r)
            {
                result += std::move(r).value();
                succeeded = true;
                break;
            }
            // Propagate hard errors immediately — only missing_field triggers fallthrough.
            if(r.error().code != resolve_errc::missing_field)
                return r;
            last = std::move(r);
        }
        if(!succeeded)
            return last;
    }

    return result;
}

// Resolves one fallback arm. An arm is either an abs:/rel: tree token, a
// category-named tree-tokenizer dispatch (${category.field}), or a literal
// string (floor value). An unrecognised category returns unknown_category so
// the ?? chain does NOT silently swallow typos (pitfall 5).
token_result tree_resolver_scope::resolve_one_arm(std::string_view arm)
{
    // Trimmed arm forms (after split_fallback_arms removes surrounding whitespace):
    //   abs:cluster/port         -> tree abs reference
    //   rel:../sibling           -> tree rel reference
    //   category.field           -> tree-tokenizer dispatch (no colon scheme)
    //   "default"                -> quoted literal (strip quotes)
    //   default                  -> unquoted literal
    auto stripped = arm;

    // Check for colon-scheme heads (abs: / rel:).
    auto colon = stripped.find(':');
    if(colon != std::string_view::npos && colon > 0)
    {
        auto scheme = stripped.substr(0, colon);
        if(scheme == "abs")
            return resolve_absolute(stripped.substr(colon + 1));
        if(scheme == "rel")
            return resolve_relative(stripped.substr(colon + 1));
    }

    // Category-named dispatch: no colon scheme, but body contains a dot.
    // lex_token parses the full ${...} form, so reconstruct the token for it.
    {
        auto dot = stripped.find('.');
        if(dot != std::string_view::npos && dot > 0)
        {
            std::string as_token = "${";
            as_token.append(stripped);
            as_token += '}';
            auto lexed = lex_token(as_token);
            if(lexed)
            {
                const std::string_view category = lexed.value().category;
                const std::string_view field    = lexed.value().name;
                if(m_tree_tokenizer == nullptr
                   || m_tree_tokenizer->find(category) == nullptr)
                {
                    return unexpected(resolve_error(resolve_errc::unknown_category,
                        nucleus::format("unknown tree tokenizer category '{}'",
                                        category)));
                }

                ++m_substitution_counter;
                if(m_substitution_counter > m_budget)
                    return unexpected(resolve_error(resolve_errc::budget_exceeded,
                        nucleus::format("reference substitution budget ({}) exceeded",
                                        m_budget)));

                const tree_tokenizer *tok = m_tree_tokenizer->find(category);
                tree_access access{m_building, m_current_path, category, field};
                return tok->resolve(access);
            }
        }
    }

    // Treat as a literal floor value (strip surrounding quotes if present).
    if(stripped.size() >= 2 && stripped.front() == '"' && stripped.back() == '"')
        return std::string(stripped.substr(1, stripped.size() - 2));

    if(stripped.empty())
        return unexpected(resolve_error(resolve_errc::missing_field,
                                  "empty fallback arm"));

    // Unquoted literal string floor.
    return std::string(stripped);
}

token_result tree_resolver_scope::resolve_absolute(std::string_view path_body)
{
    auto kp = key_path::parse(path_body);
    if(!kp)
        return unexpected(resolve_error(resolve_errc::parse_error,
                              nucleus::format("invalid abs: path '{}': {}", path_body, kp.error())));

    ++m_substitution_counter;
    if(m_substitution_counter > m_budget)
        return unexpected(resolve_error(resolve_errc::budget_exceeded,
                              nucleus::format("reference substitution budget ({}) exceeded", m_budget)));

    // Ensure the target leaf is resolved before reading its value (depth-first).
    if(m_ensure_resolved)
    {
        auto r = m_ensure_resolved(kp.value());
        if(!r)
            return unexpected(std::move(r).error());
    }

    const value *v = m_building.find(kp.value());
    if(v == nullptr)
        return unexpected(resolve_error(resolve_errc::missing_field,
                              nucleus::format("absent reference target '{}'", path_body)));

    return std::string(v->text());
}

token_result tree_resolver_scope::resolve_relative(std::string_view rel_body)
{
    key_path target = resolve_relative_path(rel_body);

    ++m_substitution_counter;
    if(m_substitution_counter > m_budget)
        return unexpected(resolve_error(resolve_errc::budget_exceeded,
                              nucleus::format("reference substitution budget ({}) exceeded", m_budget)));

    // Ensure the target leaf is resolved before reading its value (depth-first).
    if(m_ensure_resolved)
    {
        auto r = m_ensure_resolved(target);
        if(!r)
            return unexpected(std::move(r).error());
    }

    const value *v = m_building.find(target);
    if(v == nullptr)
        return unexpected(resolve_error(resolve_errc::missing_field,
                              nucleus::format("absent relative reference target '{}'",
                                             target.str())));

    return std::string(v->text());
}

// Computes the target key_path for a rel: body, applying '..' (parent) and
// '.' (current-scope, no-op) segments. The starting base is the parent of
// m_current_path (the containing scope of the leaf being resolved).
// rel:./x from "cluster/server/port" -> starts at "cluster/server", descends "x"
//          => "cluster/server/x"
// rel:../x from "cluster/server/port" -> starts at "cluster/server" (parent of leaf),
//          then ".." => "cluster", then "x" => "cluster/x"
key_path tree_resolver_scope::resolve_relative_path(std::string_view rel_body)
{
    // Base: the containing scope (parent of the current leaf).
    key_path base = m_current_path.parent();

    // Manual segment split on '/' (no views::split per GCC 11 floor).
    std::string_view remaining = rel_body;
    while(!remaining.empty())
    {
        std::string_view seg;
        auto slash = remaining.find('/');
        if(slash == std::string_view::npos)
        {
            seg = remaining;
            remaining = std::string_view{};
        }
        else
        {
            seg = remaining.substr(0, slash);
            remaining = remaining.substr(slash + 1);
        }

        if(seg == "..")
        {
            // Walk upward. If already at root (empty), further .. is above root.
            if(base.empty())
                return key_path{};
            base = base.parent();
        }
        else if(seg == ".")
        {
            // Current scope — no movement.
        }
        else if(!seg.empty())
        {
            base = base.child(std::string(seg));
        }
    }

    return base;
}

}
