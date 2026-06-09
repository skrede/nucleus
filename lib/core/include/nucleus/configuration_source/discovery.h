#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_DISCOVERY_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_DISCOVERY_H

#include "nucleus/configuration_source/path_text.h"
#include "nucleus/configuration_source/extension_registry.h"

#include <string>
#include <vector>
#include <utility>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace nucleus {

// One discovered candidate: the path that was found and the extension that
// matched it (so a caller can attribute or order by format if it wishes).
struct discovered_source
{
    std::string path;
    std::string extension;
};

// The mechanism, never the policy. The host supplies WHAT to look for (a base
// name, with no extension), WHERE to look (an ordered list of search
// directories), and WHICH formats are understood (the extension registry). The
// core does the cross product -- for each directory, for each claimed extension,
// the candidate `<dir>/<base><ext>` -- and reports the ones that exist on disk,
// in the order the host's directories and the registry imply.
//
// The core ships NO default base name, NO default directory, NO hardcoded
// filename: a host that wants "config.cfg under the exe directory" expresses that
// by passing "config" and that directory, and registering ".cfg". Precedence is
// the host's to impose by ordering its directories; this preserves that order.
class discovery
{
public:
    // Finds existing candidate files for `base_name` across `search_paths`,
    // restricted to the extensions the registry understands. Order: outer loop
    // over search_paths (host precedence), inner loop over claimed extensions.
    [[nodiscard]] static std::vector<discovered_source>
    find(std::string_view base_name,
         const std::vector<std::filesystem::path> &search_paths,
         const extension_registry &registry)
    {
        const std::vector<std::string> extensions = registry.extensions();
        std::vector<discovered_source> found;
        for(const std::filesystem::path &dir : search_paths)
        {
            for(const std::string &ext : extensions)
            {
                std::filesystem::path candidate = dir / (std::string(base_name) + ext);
                std::error_code ec;
                if(std::filesystem::is_regular_file(candidate, ec))
                    found.push_back({path_to_text(candidate), ext});
            }
        }
        return found;
    }

    // Builds source handles for every discovered candidate using the registry's
    // parser factories. The returned handles are ready to fold, in discovery order.
    [[nodiscard]] static std::vector<source_handle>
    open_all(std::string_view base_name,
             const std::vector<std::filesystem::path> &search_paths,
             const extension_registry &registry)
    {
        std::vector<source_handle> sources;
        for(const discovered_source &hit : find(base_name, search_paths, registry))
            if(auto src = registry.open(hit.path))
                sources.push_back(std::move(*src));
        return sources;
    }
};

}

#endif
