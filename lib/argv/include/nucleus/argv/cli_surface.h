#ifndef HPP_GUARD_NUCLEUS_ARGV_CLI_SURFACE_H
#define HPP_GUARD_NUCLEUS_ARGV_CLI_SURFACE_H

#include "nucleus/expected.h"

#include "nucleus/schema/cli_flag.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <utility>
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
//   * the delimiter (default `-`) is ALWAYS the path separator; every
//     occurrence maps to the keyspace separator `/`. Multi-word segments use
//     underscores, which pass through untouched. So
//     `--net-udp-auth_mode=auth` -> key `net/udp/auth_mode` = `auth`.
//
// Segments cannot contain the delimiter, and there is NO longest-match
// disambiguation and NO escaping. That restriction is precisely what makes the
// bijection clean: delimiter <-> `/` is unambiguous, so a key path projects back
// to exactly one flag. For the same reason a raw `/` in a flag is rejected
// whenever the delimiter is not `/` itself.

// The result of normalizing one token: the mapped key path and its value text.
struct cli_assignment
{
    key_path key;
    std::string value;
};

using cli_normalize_result = expected<cli_assignment, std::string>;

// Normalizes a single argv token into a (key path -> value) assignment, applying
// the rules above. Reports an error for tokens that are not `--` flags or that
// have an empty/ malformed key.
inline cli_normalize_result normalize_arg(std::string_view raw,
                                                        const cli_delimiter &delimiter = {})
{
    if(!raw.starts_with("--"))
        return unexpected(std::string("CLI argument '") + std::string(raw)
                    + "' does not start with '--'");

    const std::string_view body = raw.substr(2);
    if(body.empty())
        return unexpected(std::string("CLI argument '--' has no flag body"));

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
        return unexpected(std::string("CLI argument '") + std::string(raw)
                    + "' has an empty flag name");

    // A raw separator would survive the mapping and forge path structure the
    // delimiter did not spell, breaking invertibility.
    if(!delimiter.is_separator()
       && lhs.find(key_path::separator) != std::string_view::npos)
        return unexpected(std::string("CLI argument '") + std::string(raw)
                    + "' contains the keyspace separator '/'; the delimiter is '"
                    + delimiter.str() + "'");

    // Every delimiter occurrence -> path separator; everything else (including
    // `_`) passes through.
    const std::string &delim = delimiter.str();
    std::string key_text;
    key_text.reserve(lhs.size());
    std::size_t start = 0;
    for(std::size_t pos = lhs.find(delim); pos != std::string_view::npos;
        pos = lhs.find(delim, start))
    {
        key_text.append(lhs.substr(start, pos - start));
        key_text.push_back(key_path::separator);
        start = pos + delim.size();
    }
    key_text.append(lhs.substr(start));

    auto path = key_path::parse(key_text);
    if(!path)
        return unexpected(std::move(path).error());

    return cli_assignment{std::move(path).value(), std::move(value)};
}

}

#endif
