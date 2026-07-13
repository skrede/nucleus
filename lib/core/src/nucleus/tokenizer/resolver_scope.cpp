#include "nucleus/format.h"

#include "nucleus/tokenizer/scope_keys.h"
#include "nucleus/tokenizer/token_lexer.h"
#include "nucleus/tokenizer/resolver_scope.h"
#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/tokenizer/tree_tokenizer_registry.h"

#include <ranges>
#include <optional>

namespace nucleus {

namespace {

// Brace- and quote-aware scan for the next outer ${...} token in `input` from
// `pos`. A nested ${...} inside the outer token's argument list bumps the brace
// depth so the scan returns the OUTER closing brace, and a quoted run shields a
// stray '{' or '}' from perturbing the depth. Returns nullopt when no further
// token start remains or an opened token never closes.
struct token_span
{
    std::size_t start;
    std::size_t end;
};

// When the token's head (the category.name part, before any top-level argument
// list) contains a nested ${...}, returns the token body for whole-body
// re-expansion. Returns nullopt when no nesting precedes the first top-level
// '(' -- the function-form case, where nesting lives in arguments resolved
// separately. Scans the body honoring quotes and nested-brace depth.
std::optional<std::string> body_head_has_nested_token(std::string_view token)
{
    if(token.size() < 3) return std::nullopt;
    std::string_view const body = token.substr(2, token.size() - 3);

    char quote_char = '\0';
    int brace_depth = 0;
    bool saw_nested = false;
    for(std::size_t i = 0; i < body.size(); ++i)
    {
        char const c = body[i];
        if(quote_char != '\0')
        {
            if(c == quote_char) quote_char = '\0';
            continue;
        }
        if(c == '\'' || c == '"') { quote_char = c; continue; }
        if(c == '$' && i + 1 < body.size() && body[i + 1] == '{') { ++brace_depth; saw_nested = true; ++i; continue; }
        if(c == '}' && brace_depth > 0) { --brace_depth; continue; }
        if(brace_depth == 0 && c == '(')
            return std::nullopt;
    }
    if(saw_nested)
        return std::string(body);
    return std::nullopt;
}

std::optional<token_span> find_next_token(std::string_view input, std::size_t pos)
{
    auto start = input.find("${", pos);
    if(start == std::string_view::npos)
        return std::nullopt;

    int brace_depth = 1;
    char quote_char = '\0';
    for(std::size_t i = start + 2; i < input.size(); ++i)
    {
        char const c = input[i];
        if(quote_char != '\0')
        {
            if(c == quote_char) quote_char = '\0';
            continue;
        }
        if(c == '\'' || c == '"') { quote_char = c; continue; }
        if(c == '$' && i + 1 < input.size() && input[i + 1] == '{') { ++brace_depth; ++i; continue; }
        if(c == '}' && --brace_depth == 0)
            return token_span{start, i};
    }
    return std::nullopt;
}

}

frame_guard resolver_scope::push_file_frame(std::filesystem::path file)
{
    scope_frame f;
    f.which = scope_frame::kind::file;
    f.file_path = std::move(file);
    m_frames.push_back(std::move(f));
    return frame_guard([this] { pop_frame(); });
}

frame_guard resolver_scope::push_scope_frame(std::string category,
                                             std::unordered_map<std::string, std::string> bindings)
{
    scope_frame f;
    f.which = scope_frame::kind::generic;
    f.category = std::move(category);
    f.bindings = std::move(bindings);
    m_frames.push_back(std::move(f));
    return frame_guard([this] { pop_frame(); });
}

frame_guard resolver_scope::push_param_frame(std::unordered_map<std::string, std::string> params)
{
    scope_frame f;
    f.which = scope_frame::kind::param;
    f.bindings = std::move(params);
    m_frames.push_back(std::move(f));
    return frame_guard([this] { pop_frame(); });
}

void resolver_scope::pop_frame() noexcept
{
    if(!m_frames.empty())
        m_frames.pop_back();
}

token_result resolver_scope::lookup_frame_binding(std::string_view category,
                                                  std::string_view name) const
{
    const bool want_param = category == "args";
    for(const auto &f : m_frames | std::views::reverse)
    {
        if(want_param)
        {
            if(f.which != scope_frame::kind::param) continue;
        }
        else if(f.which != scope_frame::kind::generic || f.category != category)
        {
            continue;
        }
        auto it = f.bindings.find(std::string(name));
        if(it != f.bindings.end())
            return it->second;
        if(want_param)
            return unexpected(resolve_error(resolve_errc::missing_field,
                                      nucleus::format("unknown argument 'args.{}'", name)));
    }
    if(want_param)
        return unexpected(resolve_error(resolve_errc::out_of_scope_context,
                                  "'args' referenced outside any function frame"));
    return unexpected(resolve_error(resolve_errc::unknown_category,
                              nucleus::format("no frame or tokenizer for category '{}'", category)));
}

token_result resolver_scope::dispatch_field(std::string_view category, std::string_view name)
{
    if(category == "scope")
        return resolve_scope_key(name, m_frames);
    if(is_location_category(category))
        return resolve_location_key(category, name, m_frames);
    if(category == "args")
        return lookup_frame_binding(category, name);

    for(const auto &f : m_frames | std::views::reverse)
        if(f.which == scope_frame::kind::generic && f.category == category)
            return lookup_frame_binding(category, name);

    if(const tokenizer *t = m_registry.find(category))
        return t->resolve_field(name);
    return unexpected(resolve_error(resolve_errc::unknown_category,
                              nucleus::format("no frame or tokenizer for category '{}'", category)));
}

token_result resolver_scope::dispatch_function(std::string_view category, std::string_view name,
                                               std::span<const token_argument> args)
{
    if(const tokenizer *t = m_registry.find(category))
        return t->resolve_function(name, args);
    return unexpected(resolve_error(resolve_errc::unknown_category,
                              nucleus::format("no tokenizer for function category '{}'", category)));
}

token_result resolver_scope::resolve_one(std::string_view token)
{
    // The label held live across this whole call is the token text. A binding or
    // macro body that re-emits this exact token re-enters resolve_one while the
    // label is still on the chain, so the cycle guard fires instead of recursing
    // forever -- this is what makes a self/cyclic reference a named error.
    auto guard = m_guard.enter(std::string(token));
    if(!guard) return unexpected(std::move(guard).error());

    // Bound total substitutions across the whole load (orthogonal to the depth
    // cap): a bounded-depth exponential fanout is stopped by count, not depth.
    if(auto charged = m_budget.charge(); !charged)
        return unexpected(std::move(charged).error());

    // Field-form nesting (${env.${var}}): a nested ${...} that sits OUTSIDE any
    // argument list is resolved into the head before lexing, so the lexer sees a
    // flat category.name. Function-argument nesting (${f.g(${b})}) is left to the
    // per-argument resolve_all below. The two are told apart by whether a nested
    // ${ precedes the first top-level '('.
    std::string flattened;
    std::string_view to_lex = token;
    if(auto head_inner = body_head_has_nested_token(token); head_inner)
    {
        auto expanded = resolve_all(*head_inner);
        if(!expanded) return unexpected(std::move(expanded).error());
        flattened = "${" + std::move(expanded).value() + "}";
        to_lex = flattened;
    }

    auto lexed = lex_token(to_lex);
    if(!lexed) return unexpected(std::move(lexed).error());

    token_result produced = lexed.value().is_function
                                ? [&] {
                                      std::vector<token_argument> resolved_args;
                                      resolved_args.reserve(lexed.value().args.size());
                                      for(const auto &arg : lexed.value().args)
                                      {
                                          token_argument out{arg.name, arg.is_list, {}};
                                          out.values.reserve(arg.values.size());
                                          for(const auto &value : arg.values)
                                          {
                                              auto r = resolve_all(value);
                                              if(!r) return token_result(unexpected(std::move(r).error()));
                                              out.values.push_back(std::move(r).value());
                                          }
                                          resolved_args.push_back(std::move(out));
                                      }
                                      return dispatch_function(lexed.value().category,
                                                               lexed.value().name, resolved_args);
                                  }()
                                : dispatch_field(lexed.value().category, lexed.value().name);

    if(!produced) return produced;

    // Recursive-to-fixpoint: the produced text may itself still contain ${...}
    // (a binding whose value is a token, or the outer of a ${a${b}} once the
    // inner resolved). Re-scan it while this token's guard is still live.
    if(produced.value().find("${") != std::string::npos)
        return resolve_all(produced.value());
    return produced;
}

token_result resolver_scope::resolve_all(std::string_view input)
{
    std::string result;
    result.reserve(input.size());

    std::size_t pos = 0;
    while(pos < input.size())
    {
        auto span = find_next_token(input, pos);
        if(!span)
        {
            result.append(input.substr(pos));
            break;
        }
        result.append(input.substr(pos, span->start - pos));

        auto token = input.substr(span->start, span->end - span->start + 1);

        // Pass-1 inertness for tree-access tokens: abs:/rel: scheme tokens and
        // registered tree-tokenizer category tokens are reserved for pass-2
        // (resolve_references()). Leave them verbatim so pass-2 can process them.
        if(token.starts_with("${abs:") || token.starts_with("${rel:"))
        {
            result.append(token);
            pos = span->end + 1;
            continue;
        }
        if(m_tree_reg != nullptr)
        {
            // Extract category from "${category.field}" (between "${" and first ".").
            const std::string_view body = token.substr(2, token.size() - 3);
            const auto dot = body.find('.');
            if(dot != std::string_view::npos && dot > 0)
            {
                const std::string_view category = body.substr(0, dot);
                if(m_tree_reg->find(category) != nullptr)
                {
                    result.append(token);
                    pos = span->end + 1;
                    continue;
                }
            }
        }

        auto resolved = resolve_one(token);
        if(!resolved) return unexpected(std::move(resolved).error());
        result.append(resolved.value());
        pos = span->end + 1;
    }
    return result;
}

}
