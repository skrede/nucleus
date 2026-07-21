#ifndef HPP_GUARD_NUCLEUS_RESOLVE_CHAIN_WALKER_H
#define HPP_GUARD_NUCLEUS_RESOLVE_CHAIN_WALKER_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/schema/projection.h"

#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/inherit_declaration.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <filesystem>
#include <functional>
#include <system_error>
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

    // One entry in the expanded chain: the path the source was built from, the
    // erased source handle, and the batch the walk-pull already produced from it.
    // Owned by the caller. The walk pulls exactly once, both to surface the
    // inheritance declaration and to produce this cached batch; the fold consumes
    // the batch directly instead of pulling the handle a second time.
    struct chain_entry
    {
        std::string         path;
        source_handle       src;
        config_source_batch cached_batch;
    };

        // Factory type: given a path string, return a source_handle ready to fold.
    using factory_fn = std::function<source_handle(const std::string &)>;

    // Expands `requested_paths` into a root-first ordered chain. For each path
    // the walker follows the inheritance declaration recursively before appending
    // the declaring source, so index 0 is the deepest ancestor and the last entry
    // is the requested file itself. The factory and projection are the same ones
    // used by the load() caller. Errors propagate immediately on the first failure.
    static expected<std::vector<chain_entry>, error>
    expand(const std::vector<std::string> &requested_paths,
           const factory_fn &make,
           const schema_projection &projection,
           const inherit_policy &policy)
    {
        chain_walker walker(policy);
        // Collect the normalized form of every initially-requested path before
        // expansion begins, so the admissibility exemption below is keyed on
        // "was this ever directly requested" rather than on visit order --
        // the same document must be exempt whether it is walked as a request
        // first or reached as another request's inherited parent first. The
        // non-throwing overload skips an unnormalizable path here; expand_one()
        // reports it as a typed error below.
        for(const std::string &path : requested_paths)
        {
            std::error_code ec;
            std::string norm =
                std::filesystem::weakly_canonical(path, ec).generic_string();
            if(!ec)
                walker.m_requested.insert(std::move(norm));
        }
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

    expected<depth_guard, error> push_depth()
    {
        if(m_depth >= m_cap)
            return unexpected(error{errc::invalid_inheritance, nucleus::format(
                "inheritance chain depth {} exceeded the configured limit of {}; "
                "raise the depth cap if intentional",
                m_depth, m_cap)});
        ++m_depth;
        return depth_guard(this);
    }

    expected<path_guard, error> push_path(
        const std::filesystem::path &absolute)
    {
        std::string key = absolute.generic_string();
        if(m_visited.contains(key))
            return unexpected(error{errc::invalid_inheritance, nucleus::format(
                "inheritance cycle detected at '{}': this file was already visited "
                "in the current chain",
                key)});
        m_visited.insert(key);
        return path_guard(this, std::move(key));
    }

    // Recursively expands one path: recurses into the parent (if declared) before
    // appending this source. On return, out[] contains root-first entries.
    // is_parent is false for the top-level requested source and true for every
    // recursive call that follows an inherit= declaration; the admissibility
    // callback is invoked only when is_parent is true.
    expected<void, error>
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
            return unexpected(error{errc::invalid_inheritance, nucleus::format(
                "inheritance chain: could not normalize path '{}'", path)});
        }

        auto pg = push_path(std::filesystem::path(norm));
        if(!pg)
            return unexpected(std::move(pg).error());

        auto dg = push_depth();
        if(!dg)
            return unexpected(std::move(dg).error());

        // A document already appended to out (reached earlier by a different
        // route -- e.g. directly requested AND recursively reached as another
        // requested path's inherited parent) has already been fully walked and
        // pulled; skip the walk-pull and the recursive inherit= walk entirely so
        // it contributes exactly one chain_entry, at the layer of its first visit.
        if(m_expanded.contains(norm))
            return {};
        m_expanded.insert(norm);

        // Build the source handle via the host factory.
        source_handle handle = make(path);

        // The pull below produces this document's batch once; it is cached on
        // chain_entry so the fold consumes it directly instead of pulling the
        // same handle a second time.
        handle.apply_projection(projection);

        // A pull failure is already a typed source error (unreadable, malformed);
        // forward its code and add the chain context to the message.
        config_source_result pulled = handle.pull();
        if(!pulled)
            return unexpected(error{pulled.error().code, nucleus::format(
                "inheritance chain: source '{}': {}", path, pulled.error().message)});

        // Query the inheritance declaration AFTER pull() (arena is populated).
        inherit_declaration const decl = handle.inheritance();

        // Recurse into the parent BEFORE appending this source (root-first order).
        if(decl.which == inherit_declaration::kind::parent_path)
        {
            // Resolve relative paths against the declaring file's directory, then
            // record the lexically-normalized (filesystem-independent) form as the
            // chain label. weakly_canonical is applied only as the cycle key inside
            // expand_one, so the recorded provenance path stays portable and
            // deterministic across platforms and working directories.
            std::filesystem::path raw(decl.path);
            if(raw.is_relative())
                raw = std::filesystem::path(path).parent_path() / raw;

            auto rec = expand_one(raw.lexically_normal().generic_string(),
                                  make, projection, policy, out, true);
            if(!rec)
                return unexpected(std::move(rec).error());
        }
        // kind::opt_out terminates the chain below this file (no recursion).
        // kind::inherit_default means "no parent declared" -- the chain terminates here.

        // Admissibility check: invoked only for candidate parent sources; a
        // source that appears anywhere in the initially-requested set is exempt
        // regardless of the order or role it is first reached in, so the outcome
        // does not depend on whether it is walked as a request or as a parent first.
        if(is_parent && m_admissibility && !m_requested.contains(norm))
        {
            // Pull capabilities for the admissibility check via the handle.
            std::string reason = m_admissibility(handle.capabilities());
            if(!reason.empty())
                return unexpected(error{errc::invalid_inheritance, nucleus::format(
                    "chain admissibility check rejected parent '{}': {}", path, reason)});
        }

        // Append this source AFTER its parent (root-first). The handle is move-only;
        // the walk-pull's batch travels with it so the fold never re-pulls.
        out.push_back(chain_entry{path, std::move(handle), std::move(pulled).value()});

        // depth_guard and path_guard released here by RAII.
        return {};
    }

    std::size_t m_depth = 0;
    std::size_t m_cap;
    std::unordered_set<std::string> m_visited;
    // Canonical keys of every document already appended to out across the whole
    // expand() call. Unlike m_visited (RAII-released per top-level requested
    // path), this set is never erased -- it spans all requested_paths so the
    // same document reached via two different routes is walked/pulled once.
    std::unordered_set<std::string> m_expanded;
    // Normalized paths of every source in the initially-requested set, populated
    // once before expansion begins (see expand()). Order-independent: a document
    // is exempt from admissibility whenever it is a member of this set, regardless
    // of which role (request or parent) reaches it first during the walk.
    std::unordered_set<std::string> m_requested;
    std::function<std::string(capability_descriptor)> m_admissibility;
};

}

#endif
