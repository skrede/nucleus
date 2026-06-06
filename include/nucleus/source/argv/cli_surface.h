#ifndef HPP_GUARD_NUCLEUS_SOURCE_ARGV_CLI_SURFACE_H
#define HPP_GUARD_NUCLEUS_SOURCE_ARGV_CLI_SURFACE_H

#include "nucleus/result.h"
#include "nucleus/keyspace/key_path.h"

#include <string>
#include <utility>
#include <algorithm>
#include <string_view>

namespace nucleus {

// The syntactic mapping of one CLI token onto a keyspace key path -- the
// `--a-b-c=v <-> a/b/c` half of the bijection. It is deliberately a pure,
// schema-free transform: the schema validation is a SEPARATE, LATER step (see
// argv_source), which is what keeps the segmentation simple and the bijection
// invertible.
//
// The rules (matching the proven precedent):
//   * require a leading `--`; strip it.
//   * split on the FIRST `=` only: lhs is the key, rhs is everything after
//     (so a value may itself contain `=`).
//   * a bare flag with no `=` becomes a truthy presence value, the string
//     "true".
//   * `-` is ALWAYS the path separator; it maps to the keyspace separator `/`.
//     multi-word segments use underscores, which pass through untouched. So
//     `--plexus-udp-auth_mode=auth` -> key `plexus/udp/auth_mode` = `auth`.
//
// Segments cannot contain hyphens, and there is NO longest-match disambiguation
// and NO escaping. That restriction is precisely what makes the bijection clean:
// `-` <-> `/` is unambiguous, so a key path projects back to exactly one flag.

// The result of normalizing one token: the mapped key path and its value text.
struct cli_assignment
{
    key_path key;
    std::string value;
};

using cli_normalize_result = result<cli_assignment, std::string>;

// Normalizes a single argv token into a (key path -> value) assignment, applying
// the rules above. Reports an error for tokens that are not `--` flags or that
// have an empty/ malformed key.
[[nodiscard]] inline cli_normalize_result normalize_arg(std::string_view raw)
{
    if(!raw.starts_with("--"))
        return fail(std::string("CLI argument '") + std::string(raw)
                    + "' does not start with '--'");

    std::string_view body = raw.substr(2);
    if(body.empty())
        return fail(std::string("CLI argument '--' has no flag body"));

    std::string_view lhs;
    std::string value;
    if(auto eq = body.find('='); eq != std::string_view::npos)
    {
        lhs = body.substr(0, eq);
        value = std::string(body.substr(eq + 1));
    }
    else
    {
        lhs = body;
        value = "true"; // bare flag = truthy presence
    }

    if(lhs.empty())
        return fail(std::string("CLI argument '") + std::string(raw)
                    + "' has an empty flag name");

    // `-` -> path separator; everything else (including `_`) passes through.
    std::string key_text;
    key_text.reserve(lhs.size());
    std::transform(lhs.begin(), lhs.end(), std::back_inserter(key_text),
                   [](char c) { return c == '-' ? key_path::separator : c; });

    auto path = key_path::parse(key_text);
    if(!path)
        return fail(std::move(path).error());

    return cli_assignment{std::move(path).value(), std::move(value)};
}

// The inverse projection: a keyspace path back to its canonical CLI flag. Because
// segments never contain hyphens, this is a total, lossless inverse of the
// segmentation -- the bijection made explicit (and the basis for the schema-
// projected flag surface and, later, tab completion).
[[nodiscard]] inline std::string flag_of(const key_path &path)
{
    std::string flag = "--";
    const auto &segments = path.segments();
    for(std::size_t i = 0; i < segments.size(); ++i)
    {
        if(i != 0)
            flag.push_back('-');
        flag.append(segments[i]);
    }
    return flag;
}

}

#endif
