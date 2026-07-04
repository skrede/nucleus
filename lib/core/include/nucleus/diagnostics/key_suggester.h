#ifndef HPP_GUARD_NUCLEUS_DIAGNOSTICS_KEY_SUGGESTER_H
#define HPP_GUARD_NUCLEUS_DIAGNOSTICS_KEY_SUGGESTER_H

#include "nucleus/keyspace/key_path.h"

#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <string_view>

namespace nucleus {

// "Did you mean ...?" for an unknown key. When a source emits a key the schema
// does not declare, the nearest declared keys (by edit distance) are the most
// useful thing to show -- a typo is one or two edits from its intended target.
//
// The metric is a weighted Levenshtein distance: a substitution between two
// characters of the same class (both lowercase letters, both digits, or both the
// path separator) costs half of a cross-class substitution. So a one-letter typo
// like `cache` <-> `cacho` (letter-for-letter, cost 0.5) ranks ahead of
// `cache` <-> `cach3` (a letter swapped for a digit is cross-class, cost 1.0).
// This is generic string machinery with no host vocabulary -- it operates purely
// on the `/`-separated key text.

// Three character classes for the substitution-weight rule. Anything outside
// them (notably `_`) falls to the cross-class weight.
constexpr int key_char_class(char c) noexcept
{
    if(c >= 'a' && c <= 'z') return 1;
    if(c >= '0' && c <= '9') return 2;
    if(c == key_path::separator) return 3;
    return 0;
}

// The weighted edit distance between two key strings.
inline double weighted_levenshtein(std::string_view a, std::string_view b)
{
    const std::size_t m = a.size();
    const std::size_t n = b.size();
    std::vector<double> prev(n + 1);
    std::vector<double> curr(n + 1);
    for(std::size_t j = 0; j <= n; ++j)
        prev[j] = static_cast<double>(j);
    for(std::size_t i = 1; i <= m; ++i)
    {
        curr[0] = static_cast<double>(i);
        for(std::size_t j = 1; j <= n; ++j)
        {
            const char ca = a[i - 1];
            const char cb = b[j - 1];
            const int ka = key_char_class(ca);
            const int kb = key_char_class(cb);
            const double sub = [&] {
                if(ca == cb)
                    return 0.0;
                if(ka != 0 && ka == kb)
                    return 0.5;
                return 1.0;
            }();
            curr[j] = std::min({prev[j] + 1.0, curr[j - 1] + 1.0, prev[j - 1] + sub});
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// The up-to-`limit` nearest known keys to `unknown`, closest first. Ties break
// lexicographically so the suggestion order is deterministic. An empty candidate
// set or a zero limit yields no suggestions.
inline std::vector<std::string> suggest_keys(
    std::string_view unknown,
    std::span<const std::string> known,
    std::size_t limit = 3)
{
    if(known.empty() || limit == 0)
        return {};

    struct scored
    {
        double cost;
        std::string key;
    };
    std::vector<scored> ranked;
    ranked.reserve(known.size());
    for(const std::string &candidate : known)
        ranked.push_back({weighted_levenshtein(unknown, candidate), candidate});

    std::sort(ranked.begin(), ranked.end(), [](const scored &lhs, const scored &rhs) {
        if(lhs.cost < rhs.cost)
            return true;
        if(rhs.cost < lhs.cost)
            return false;
        return lhs.key < rhs.key;
    });

    const std::size_t take = std::min(limit, ranked.size());
    std::vector<std::string> out;
    out.reserve(take);
    for(std::size_t i = 0; i < take; ++i)
        out.push_back(std::move(ranked[i].key));
    return out;
}

}

#endif
