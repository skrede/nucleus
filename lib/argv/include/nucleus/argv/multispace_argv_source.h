#ifndef HPP_GUARD_NUCLEUS_ARGV_MULTISPACE_ARGV_SOURCE_H
#define HPP_GUARD_NUCLEUS_ARGV_MULTISPACE_ARGV_SOURCE_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/log_sink.h"
#include "nucleus/capability.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/cli_surface.h"
#include "nucleus/configuration_source/argv/key_recognizer.h"

#include <string>
#include <vector>
#include <stdexcept>
#include <string_view>

namespace nucleus {

// Partitions one argv token vector across named configuration spaces by the flag's
// first path segment. Every flag MUST begin with a registered space name as its
// first segment (e.g. --alpha-x=1 for space "alpha" with delimiter "-"); a flag
// whose first segment names NO registered space is a loud pull error. Flags
// addressed to another registered space are silently skipped by the current view.
class multispace_argv_source
{
public:
    explicit multispace_argv_source(std::vector<std::string> args)
        : m_args(std::move(args))
    {
    }

    // Registers a space name. Order of registration is preserved for error messages.
    multispace_argv_source &register_space(std::string name)
    {
        m_spaces.push_back(std::move(name));
        return *this;
    }

    // A lightweight view of this source projected to one registered space.
    // Satisfies the configuration_source concept by duck typing. Holds a back-pointer
    // to the owner (the owner must outlive the view) plus per-view options.
    class space_view
    {
    public:
        space_view(const multispace_argv_source *owner,
                   std::vector<std::string> spaces,
                   std::string target)
            : m_owner(owner)
            , m_spaces(std::move(spaces))
            , m_target(std::move(target))
        {
        }

        space_view &recognize_with(key_recognizer recognizer)
        {
            m_recognizer = std::move(recognizer);
            return *this;
        }

        space_view &delimit_with(cli_delimiter delimiter)
        {
            m_delimiter = std::move(delimiter);
            return *this;
        }

        space_view &anchor_at(key_path anchor)
        {
            m_anchor = std::move(anchor);
            return *this;
        }

        space_view &policy(unknown_key_policy p) noexcept
        {
            m_policy = p;
            return *this;
        }

        space_view &log_to(log_sink &sink) noexcept
        {
            m_log = &sink;
            return *this;
        }

        [[nodiscard]] static capability_descriptor descriptor() noexcept
        {
            return capability_descriptor{capability::nesting, capability::duplicate_keys};
        }

        [[nodiscard]] capability_descriptor capabilities() const { return descriptor(); }

        [[nodiscard]] configuration_source_result pull()
        {
            configuration_source_batch batch;
            batch.entries.reserve(m_owner->m_args.size());

            for(const std::string &token : m_owner->m_args)
            {
                auto mapped = normalize_arg(token, m_delimiter);
                if(!mapped)
                    return unexpected(configuration_source_error{
                        errc::malformed_source, std::move(mapped).error()});

                const key_path &path = mapped.value().key;

                // A bare space name (single segment) is not a valid addressed flag.
                if(path.size() < 2)
                {
                    if(!path.empty() && is_registered_space(path.front()))
                        continue; // bare space name -- belongs to this or another space, skip
                    return unexpected(configuration_source_error{errc::schema_violation,
                        nucleus::format(
                            "unaddressed CLI flag '{}': first segment '{}' is not a "
                            "registered space name; registered spaces: {}",
                            token, path.empty() ? "" : path.front(),
                            comma_list())});
                }

                const std::string &first = path.front();

                if(first == m_target)
                {
                    // Strip the space-name prefix.
                    key_path stripped = path.relative_to(key_path(std::vector<std::string>{first}));

                    if(!m_anchor.empty())
                        stripped = m_anchor.join(stripped);

                    const bool recognized = !m_recognizer || m_recognizer(stripped);
                    if(!recognized)
                    {
                        if(m_policy == unknown_key_policy::strict)
                            return unexpected(configuration_source_error{
                                errc::schema_violation, nucleus::format(
                                    "unknown CLI flag '{}' maps to undeclared key '{}'",
                                    token, stripped.str())});
                        if(m_log)
                            m_log->log(log_level::warn, nucleus::format(
                                "unknown CLI flag '{}'; lenient mode -- stored as string at '{}'",
                                token, stripped.str()));
                    }

                    batch.entries.push_back(make_entry(
                        stripped.str(), value::owned(std::move(mapped.value().value)),
                        descriptor()));
                }
                else if(is_registered_space(first))
                {
                    continue; // addressed to another registered space; that view will pull it
                }
                else
                {
                    return unexpected(configuration_source_error{errc::schema_violation,
                        nucleus::format(
                            "unaddressed CLI flag '{}': first segment '{}' is not a "
                            "registered space name; registered spaces: {}",
                            token, first, comma_list())});
                }
            }

            return batch;
        }

    private:
        [[nodiscard]] bool is_registered_space(const std::string &segment) const
        {
            for(const std::string &s : m_spaces)
                if(s == segment)
                    return true;
            return false;
        }

        [[nodiscard]] std::string comma_list() const
        {
            std::string out;
            for(std::size_t i = 0; i < m_spaces.size(); ++i)
            {
                if(i != 0)
                    out += ", ";
                out += m_spaces[i];
            }
            return out;
        }

        const multispace_argv_source *m_owner;
        std::vector<std::string> m_spaces;
        std::string m_target;
        cli_delimiter m_delimiter;
        key_path m_anchor;
        key_recognizer m_recognizer;
        unknown_key_policy m_policy = unknown_key_policy::strict;
        log_sink *m_log = nullptr;
    };

    // Returns a space_view for the named space. Throws std::invalid_argument when
    // `name` has not been registered (programming error, not a runtime pull failure).
    [[nodiscard]] space_view for_space(std::string_view name)
    {
        for(const std::string &s : m_spaces)
            if(s == name)
                return space_view(this, m_spaces, std::string(name));
        throw std::invalid_argument(
            nucleus::format("multispace_argv_source: '{}' is not a registered space name", name));
    }

private:
    std::vector<std::string> m_args;
    std::vector<std::string> m_spaces;
};

}

#endif
