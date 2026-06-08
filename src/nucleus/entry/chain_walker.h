#ifndef HPP_GUARD_NUCLEUS_ENTRY_CHAIN_WALKER_H
#define HPP_GUARD_NUCLEUS_ENTRY_CHAIN_WALKER_H

#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/schema/projection.h"

#include "nucleus/source/source.h"
#include "nucleus/source/inherit_declaration.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <filesystem>
#include <functional>
#include <unordered_set>

namespace nucleus {

// Transient walker that expands a flat list of requested paths into a
// root-first ordered chain of pulled sources, following each source's
// inheritance declaration. Depth and cycle guards are RAII -- they are
// released on function exit regardless of the return path. The walker is
// constructed once per expand() call and is not reused.
class chain_walker
{
public:
    // Move-only RAII guard: decrements the walker's depth counter on destroy.
    struct depth_guard
    {
        chain_walker *m_walker = nullptr;

        depth_guard() = default;
        explicit depth_guard(chain_walker *w) : m_walker(w) {}
        depth_guard(depth_guard &&o) noexcept : m_walker(o.m_walker) { o.m_walker = nullptr; }
        depth_guard &operator=(depth_guard &&o) noexcept
        {
            if(this != &o) { release(); m_walker = o.m_walker; o.m_walker = nullptr; }
            return *this;
        }
        ~depth_guard() noexcept { release(); }
        void release() noexcept { if(m_walker) { --m_walker->m_depth; m_walker = nullptr; } }

        depth_guard(const depth_guard &) = delete;
        depth_guard &operator=(const depth_guard &) = delete;
    };

    // Move-only RAII guard: removes the path from the visited set on destroy.
    struct path_guard
    {
        chain_walker *m_walker = nullptr;
        std::string m_key;

        path_guard() = default;
        path_guard(chain_walker *w, std::string key)
            : m_walker(w), m_key(std::move(key)) {}
        path_guard(path_guard &&o) noexcept
            : m_walker(o.m_walker), m_key(std::move(o.m_key)) { o.m_walker = nullptr; }
        path_guard &operator=(path_guard &&o) noexcept
        {
            if(this != &o)
            {
                release();
                m_walker = o.m_walker;
                m_key    = std::move(o.m_key);
                o.m_walker = nullptr;
            }
            return *this;
        }
        ~path_guard() noexcept { release(); }
        void release() noexcept
        {
            if(m_walker) { m_walker->m_visited.erase(m_key); m_walker = nullptr; }
        }

        path_guard(const path_guard &) = delete;
        path_guard &operator=(const path_guard &) = delete;
    };

    // One entry in the expanded chain: the path the source was built from and the
    // source itself. Owned by the caller. The walk pulls once to surface the
    // inheritance declaration; the batch is discarded -- the fold performs the
    // consuming pull.
    struct chain_entry
    {
        std::string path;
        std::unique_ptr<source> src;
    };

    // Factory type: given a path string, return a non-null unique_ptr<source>
    // or null (null is surfaced as a load error). This matches
    // configuration_space::document_factory.
    using factory_fn = std::function<std::unique_ptr<source>(const std::string &)>;

    // Expands `requested_paths` into a root-first ordered chain. For each path
    // the walker follows the inheritance declaration recursively before appending
    // the declaring source, so index 0 is the deepest ancestor and the last entry
    // is the requested file itself. The factory and projection are the same ones
    // used by the load() caller. Errors propagate immediately on the first failure.
    [[nodiscard]] static expected<std::vector<chain_entry>, std::string>
    expand(const std::vector<std::string> &requested_paths,
           const factory_fn &make,
           const schema_projection &projection,
           const inherit_policy &policy)
    {
        chain_walker walker(policy);
        std::vector<chain_entry> out;
        for(const std::string &path : requested_paths)
        {
            auto res = walker.expand_one(path, make, projection, policy, out);
            if(!res)
                return unexpected(std::move(res).error());
        }
        return out;
    }

private:
    explicit chain_walker(const inherit_policy &policy)
        : m_cap(policy.depth_cap)
        , m_admissibility(policy.admissibility)
    {}

    [[nodiscard]] expected<depth_guard, std::string> push_depth()
    {
        if(m_depth >= m_cap)
            return unexpected(nucleus::format(
                "inheritance chain depth {} exceeded the configured limit of {}; "
                "raise the depth cap if intentional",
                m_depth, m_cap));
        ++m_depth;
        return depth_guard(this);
    }

    [[nodiscard]] expected<path_guard, std::string> push_path(
        const std::filesystem::path &absolute)
    {
        std::string key = absolute.generic_string();
        if(m_visited.count(key))
            return unexpected(nucleus::format(
                "inheritance cycle detected at '{}': this file was already visited "
                "in the current chain",
                key));
        m_visited.insert(key);
        return path_guard(this, std::move(key));
    }

    // Recursively expands one path: recurses into the parent (if declared) before
    // appending this source. On return, out[] contains root-first entries.
    // is_parent is false for the top-level requested source and true for every
    // recursive call that follows an inherit= declaration; the admissibility
    // callback is invoked only when is_parent is true.
    [[nodiscard]] expected<std::monostate, std::string>
    expand_one(const std::string &path,
               const factory_fn &make,
               const schema_projection &projection,
               const inherit_policy &policy,
               std::vector<chain_entry> &out,
               bool is_parent = false)
    {
        // Normalize the path to get a stable key for the cycle guard.
        std::string norm;
        try
        {
            norm = std::filesystem::weakly_canonical(path).generic_string();
        }
        catch(...)
        {
            return unexpected(nucleus::format(
                "inheritance chain: could not normalize path '{}'", path));
        }

        auto pg = push_path(std::filesystem::path(norm));
        if(!pg)
            return unexpected(std::move(pg).error());

        auto dg = push_depth();
        if(!dg)
            return unexpected(std::move(dg).error());

        // Build the source via the host factory.
        std::unique_ptr<source> src = make ? make(path) : nullptr;
        if(!src)
            return unexpected(nucleus::format(
                "inheritance chain: no source could be built for path '{}'", path));

        src->apply_projection(projection);

        source_result pulled = src->pull();
        if(!pulled)
            return unexpected(nucleus::format(
                "inheritance chain: source '{}': {}", path, pulled.error()));

        // Query the inheritance declaration AFTER pull() (arena is populated).
        inherit_declaration decl = src->inheritance();

        // Recurse into the parent BEFORE appending this source (root-first order).
        if(decl.which == inherit_declaration::kind::parent_path)
        {
            // Resolve relative paths against the declaring file's directory.
            std::filesystem::path raw(decl.path);
            if(raw.is_relative())
                raw = std::filesystem::path(path).parent_path() / raw;

            std::string parent_path_str;
            try
            {
                parent_path_str = std::filesystem::weakly_canonical(raw).generic_string();
            }
            catch(...)
            {
                return unexpected(nucleus::format(
                    "inheritance chain: could not normalize parent path '{}' "
                    "declared by '{}'",
                    decl.path, path));
            }

            auto rec = expand_one(parent_path_str, make, projection, policy, out, true);
            if(!rec)
                return unexpected(std::move(rec).error());
        }
        // kind::opt_out terminates the chain below this file (no recursion).
        // kind::inherit_default means "no parent declared" -- the chain terminates here.

        // Admissibility check runs after pull() but before appending to output.
        // Invoked only for candidate parent sources (is_parent=true); the initially
        // requested source is never subject to the admissibility policy.
        if(is_parent && m_admissibility)
        {
            std::string reason = m_admissibility(*src);
            if(!reason.empty())
                return unexpected(nucleus::format(
                    "chain admissibility check rejected parent '{}': {}", path, reason));
        }

        // Append this source AFTER its parent (root-first).
        out.push_back(chain_entry{path, std::move(src)});

        // depth_guard and path_guard released here by RAII.
        return std::monostate{};
    }

    std::size_t m_depth = 0;
    std::size_t m_cap;
    std::unordered_set<std::string> m_visited;
    std::function<std::string(const source &)> m_admissibility;
};

}

#endif
