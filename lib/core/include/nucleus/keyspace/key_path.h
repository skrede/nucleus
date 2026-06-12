#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_KEY_PATH_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_KEY_PATH_H

#include "nucleus/expected.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <string_view>

namespace nucleus {

// The hierarchical key path -- the spine. A config is one keyspace
// addressed by `/`-separated FQN-style paths (a/b/c). This value-type interprets
// the flat `/`-separated string the source seam already emits (keyspace_entry's
// path); it does NOT introduce a second path representation. It carries the
// segment decomposition that the hierarchy, the schema anchors, and the CLI
// bijection all share.
//
// The separator is `/`, fixed and source-neutral: a flat source (env, argv)
// emits single-segment paths; a document source emits nested ones. A segment is
// never empty and never itself contains the separator -- that is what keeps the
// CLI bijection (`-` <-> `/`) invertible.
class key_path
{
public:
    static constexpr char separator = '/';

    key_path() = default;

    // Builds a path from already-validated segments. Used by the parser/CLI
    // surface once it has split a flag into clean segments.
    explicit key_path(std::vector<std::string> segments)
        : m_segments(std::move(segments))
    {
    }

    // Parses a `/`-separated FQN string into segments. Leading/trailing
    // separators and empty (`a//b`) segments are rejected so a path always has a
    // canonical, round-trippable form.
    [[nodiscard]] static expected<key_path, std::string> parse(std::string_view text)
    {
        if(text.empty())
            return unexpected(std::string("key path is empty"));

        std::vector<std::string> segments;
        std::size_t start = 0;
        for(std::size_t i = 0; i <= text.size(); ++i)
        {
            if(i == text.size() || text[i] == separator)
            {
                if(i == start)
                    return unexpected(std::string("key path '") + std::string(text)
                                + "' has an empty segment");
                segments.emplace_back(text.substr(start, i - start));
                start = i + 1;
            }
        }
        return key_path(std::move(segments));
    }

    [[nodiscard]] bool empty() const noexcept { return m_segments.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_segments.size(); }

    [[nodiscard]] const std::vector<std::string> &segments() const noexcept
    {
        return m_segments;
    }

    [[nodiscard]] const std::string &front() const { return m_segments.front(); }
    [[nodiscard]] const std::string &leaf() const { return m_segments.back(); }

    // The path one level up (a/b/c -> a/b). An empty path or a single-segment
    // path has an empty parent.
    [[nodiscard]] key_path parent() const
    {
        if(m_segments.size() <= 1)
            return key_path{};
        return key_path(std::vector<std::string>(m_segments.begin(),
                                                 m_segments.end() - 1));
    }

    // Extends this path with one more segment (a/b + c -> a/b/c).
    [[nodiscard]] key_path child(std::string segment) const
    {
        std::vector<std::string> next = m_segments;
        next.push_back(std::move(segment));
        return key_path(std::move(next));
    }

    // This path extended by every segment of `tail` (a/b join c/d -> a/b/c/d).
    [[nodiscard]] key_path join(const key_path &tail) const
    {
        std::vector<std::string> next = m_segments;
        next.insert(next.end(), tail.m_segments.begin(), tail.m_segments.end());
        return key_path(std::move(next));
    }

    // True when `prefix` is this path's leading segments (a/b/c starts with a/b,
    // with itself, and with the empty path).
    [[nodiscard]] bool starts_with(const key_path &prefix) const noexcept
    {
        if(prefix.m_segments.size() > m_segments.size())
            return false;
        return std::equal(prefix.m_segments.begin(), prefix.m_segments.end(),
                          m_segments.begin());
    }

    // The remainder after `prefix` (a/b/c relative to a -> b/c; relative to
    // itself -> empty). Precondition: starts_with(prefix).
    [[nodiscard]] key_path relative_to(const key_path &prefix) const
    {
        return key_path(std::vector<std::string>(
            m_segments.begin() + static_cast<std::ptrdiff_t>(prefix.m_segments.size()),
            m_segments.end()));
    }

    // The canonical `/`-joined string -- the same shape the source seam emits, so
    // a key_path round-trips through keyspace_entry::path.
    [[nodiscard]] std::string str() const
    {
        std::string out;
        for(std::size_t i = 0; i < m_segments.size(); ++i)
        {
            if(i != 0)
                out.push_back(separator);
            out.append(m_segments[i]);
        }
        return out;
    }

    friend bool operator==(const key_path &a, const key_path &b) noexcept
    {
        return a.m_segments == b.m_segments;
    }

    friend bool operator!=(const key_path &a, const key_path &b) noexcept
    {
        return !(a == b);
    }

private:
    std::vector<std::string> m_segments;
};

}

#endif
