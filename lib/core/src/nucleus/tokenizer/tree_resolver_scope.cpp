#include "nucleus/tokenizer/brace_scan.h"
#include "nucleus/tokenizer/tree_resolver_scope.h"

#include "nucleus/format.h"

#include "nucleus/utility/escaped_text.h"

#include <string>
#include <utility>
#include <cstddef>
#include <string_view>

namespace nucleus {

tree_resolver_scope::tree_resolver_scope(const keyspace &building,
                                         key_path current_path,
                                         substitution_budget &budget,
                                         expansion_guard &dispatch_guard,
                                         ensure_resolved_fn ensure_resolved,
                                         const tree_tokenizer_registry *tree_reg) noexcept
    : m_building(building)
    , m_current_path(std::move(current_path))
    , m_budget(budget)
    , m_ensure_resolved(std::move(ensure_resolved))
    , m_tree_tokenizer(tree_reg)
    , m_dispatch_guard(dispatch_guard)
{
}

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

        result.append(value_text.substr(pos, open - pos));

        auto const close = scan_braced_span(value_text, open + 2);
        if(close == std::string_view::npos)
            return unexpected(resolve_error(resolve_errc::parse_error,
                                      "unterminated ${ in value"));

        const std::string_view token_body = value_text.substr(open + 2, close - open - 2);
        pos = close + 1;

        // Split on top-level '??' to get fallback arms.
        std::vector<std::string_view> const arms = split_fallback_arms(token_body);

        token_result last = unexpected(resolve_error(
            resolve_errc::missing_field, "all fallback arms absent"));
        bool succeeded = false;
        for(std::size_t i = 0; i < arms.size(); ++i)
        {
            auto r = resolve_one_arm(arms[i], i > 0);
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
// string, which only a fallback arm may be. An unrecognised category returns
// unknown_category so the ?? chain does NOT silently swallow typos.
token_result tree_resolver_scope::resolve_one_arm(std::string_view arm, bool is_fallback)
{
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
                                        escaped_text(category))));
                }

                if(auto charged = m_budget.charge(); !charged)
                    return unexpected(std::move(charged).error());
                auto entered = m_dispatch_guard.enter(
                    nucleus::format("{}.{}", escaped_text(category), escaped_text(field)));
                if(!entered)
                    return unexpected(std::move(entered).error());
                const tree_tokenizer *tok = m_tree_tokenizer->find(category);
                tree_access const access{m_building, m_current_path, category, field};
                return expand_produced(tok->resolve(access));
            }
        }
    }

    if(stripped.size() >= 2 && stripped.front() == '"' && stripped.back() == '"')
        return std::string(stripped.substr(1, stripped.size() - 2));

    if(stripped.empty())
        return unexpected(resolve_error(resolve_errc::missing_field,
                                  "empty fallback arm"));

    // The unquoted floor exists for a ?? default arm. A whole token body reaching
    // it would be returned with its braces silently removed, so it is reported
    // instead; a quoted arm carries text that merely resembles a token.
    if(!is_fallback)
        return unexpected(resolve_error(resolve_errc::unknown_category,
            nucleus::format("unrecognized token body '{}'", escaped_text(stripped))));

    return std::string(stripped);
}

// Recursive-to-fixpoint, mirroring resolver_scope::resolve_one: a tokenizer's own
// output may carry a further tree token. Resume from the first ${.
token_result tree_resolver_scope::expand_produced(token_result produced)
{
    if(!produced)
        return produced;
    const std::string &out = produced.value();
    auto const splice = out.find("${");
    if(splice == std::string::npos)
        return produced;
    auto tail = resolve_value(std::string_view(out).substr(splice));
    if(!tail)
        return unexpected(std::move(tail).error());
    return out.substr(0, splice) + std::move(tail).value();
}

token_result tree_resolver_scope::resolve_absolute(std::string_view path_body)
{
    auto kp = key_path::parse(path_body);
    if(!kp)
        return unexpected(resolve_error(resolve_errc::parse_error, nucleus::format(
            "invalid abs: path '{}': {}", escaped_text(path_body), escaped_text(kp.error()))));

    if(auto charged = m_budget.charge(); !charged)
        return unexpected(std::move(charged).error());

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
                              nucleus::format("absent reference target '{}'", escaped_text(path_body))));

    return std::string(v->text());
}

token_result tree_resolver_scope::resolve_relative(std::string_view rel_body)
{
    auto resolved = resolve_relative_path(rel_body);
    if(!resolved)
        return unexpected(std::move(resolved).error());
    key_path const target = std::move(resolved).value();

    if(auto charged = m_budget.charge(); !charged)
        return unexpected(std::move(charged).error());

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
                                             escaped_text(target.str()))));

    return std::string(v->text());
}

// Computes the target key_path for a rel: body, applying '..' (parent) and
// '.' (current-scope, no-op) segments. The starting base is the parent of
// m_current_path (the containing scope of the leaf being resolved).
// rel:./x from "cluster/server/port" -> starts at "cluster/server", descends "x"
//          => "cluster/server/x"
// rel:../x from "cluster/server/port" -> starts at "cluster/server" (parent of leaf),
//          then ".." => "cluster", then "x" => "cluster/x"
expected<key_path, resolve_error>
tree_resolver_scope::resolve_relative_path(std::string_view rel_body)
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
                return unexpected(resolve_error(resolve_errc::parse_error,
                    nucleus::format("relative reference '{}' walks above the "
                                    "configuration root", escaped_text(rel_body))));
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
