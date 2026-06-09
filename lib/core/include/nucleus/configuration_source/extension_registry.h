#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_EXTENSION_REGISTRY_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_EXTENSION_REGISTRY_H

#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/identity.h"

#include "nucleus/configuration_source/configuration_source.h"

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <variant>
#include <algorithm>
#include <functional>
#include <string_view>
#include <initializer_list>

namespace nucleus {

// Builds a source for a concrete file path. A parser registration carries one of
// these; discovery invokes it to turn a found path into a live source. The core
// stays format-neutral: it never names a parser, only stores the host's factory.
using parser_factory = std::function<std::unique_ptr<configuration_source>(const std::string &path)>;

// The error a registration can produce.
using extension_error = std::string;

using extension_result = expected<std::monostate, extension_error>;

// Maps each file extension to exactly one parser.
//
// A parser may claim several extensions (one parser claims ".cfg" and
// ".conf", say). But each extension resolves to exactly one parser: a second
// registration claiming an already-claimed extension is a registration-time
// error that names the conflicting owner tokens, never a silent overwrite. The
// registry is pure mechanism -- it stores host factories and host owner tokens
// and interprets neither.
class extension_registry
{
public:
    extension_registry() = default;

    // Registers a parser that claims `extensions`. Each extension is normalized
    // to start with a single dot. If any requested extension is already claimed,
    // the whole registration is rejected (atomic: nothing is committed) with a
    // message naming the surface.
    extension_result claim(std::initializer_list<std::string_view> extensions,
                           parser_factory factory,
                           owner_token owner = {})
    {
        std::vector<std::string> normalized;
        normalized.reserve(extensions.size());
        for(std::string_view ext : extensions)
        {
            std::string key = normalize(ext);
            if(auto it = m_parsers.find(key); it != m_parsers.end())
            {
                const bool same_owner = it->second.owner == owner;
                return unexpected(nucleus::format(
                    "extension '{}' is already claimed by {} parser",
                    key, same_owner ? "the same" : "another"));
            }
            // A double-claim WITHIN one call is just as much a registration-time
            // error as colliding with an already-registered parser: the map
            // would silently no-op the second emplace, so reject it here before
            // anything is committed (the registration stays atomic).
            if(std::find(normalized.begin(), normalized.end(), key) != normalized.end())
                return unexpected(nucleus::format(
                    "extension '{}' is claimed twice in the same registration", key));
            normalized.push_back(std::move(key));
        }

        for(std::string &key : normalized)
            m_parsers.emplace(std::move(key), entry{factory, owner});
        return extension_result(std::monostate{});
    }

    [[nodiscard]] bool claims(std::string_view extension) const
    {
        return m_parsers.find(normalize(extension)) != m_parsers.end();
    }

    // Builds a source for `path` if its extension is claimed; nullptr otherwise.
    // The extension is the final dot-suffix of the path's last segment.
    [[nodiscard]] std::unique_ptr<configuration_source> open(const std::string &path) const
    {
        auto it = m_parsers.find(extension_of(path));
        if(it == m_parsers.end())
            return nullptr;
        return it->second.factory(path);
    }

    // The set of extensions currently claimed, in canonical form. Discovery reads
    // this to build its candidate set -- it never enumerates arbitrary extensions
    // of its own, only the ones a host registered here.
    [[nodiscard]] std::vector<std::string> extensions() const
    {
        std::vector<std::string> keys;
        keys.reserve(m_parsers.size());
        for(const auto &[key, parser] : m_parsers)
            keys.push_back(key);
        return keys;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_parsers.size(); }

    // Normalizes an extension to a leading-dot, lower-effort canonical form. An
    // empty string maps to "" (no extension). Exposed so discovery shares the
    // exact same normalization.
    [[nodiscard]] static std::string normalize(std::string_view extension)
    {
        if(extension.empty())
            return std::string{};
        if(extension.front() == '.')
            return std::string(extension);
        std::string out;
        out.reserve(extension.size() + 1);
        out.push_back('.');
        out.append(extension);
        return out;
    }

    // The extension of a path: the substring from the last '.' in the final path
    // segment. No dot (or a leading-dot dotfile) yields "".
    [[nodiscard]] static std::string extension_of(std::string_view path)
    {
        std::size_t slash = path.find_last_of("/\\");
        std::string_view name = slash == std::string_view::npos
                                    ? path
                                    : path.substr(slash + 1);
        std::size_t dot = name.find_last_of('.');
        if(dot == std::string_view::npos || dot == 0)
            return std::string{};
        return std::string(name.substr(dot));
    }

private:
    struct entry
    {
        parser_factory factory;
        owner_token owner;
    };

    std::map<std::string, entry> m_parsers;
};

}

#endif
