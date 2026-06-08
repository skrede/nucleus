#ifndef HPP_GUARD_NUCLEUS_SOURCE_ARGV_ARGV_SOURCE_H
#define HPP_GUARD_NUCLEUS_SOURCE_ARGV_ARGV_SOURCE_H

#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/log_sink.h"
#include "nucleus/capability.h"

#include "nucleus/source/source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"

#include "nucleus/source/argv/cli_surface.h"

#include <string>
#include <vector>
#include <utility>
#include <functional>

namespace nucleus {

// How an unrecognized CLI key (a syntactically valid flag whose mapped path the
// schema does not declare) is handled. strict is the default: the schema is the
// authority, so an unknown flag is an error. lenient mirrors the proven opt-in:
// warn through the log_sink and store the value as a string anyway.
enum class unknown_key_policy
{
    strict,
    lenient,
};

// Tests whether a mapped key path is a declared target. The argv source asks this
// AFTER syntactic mapping -- the schema-authoritative validation is a separate,
// later step from segmentation. Supplied by the host (it bridges to the schema
// registry's surface) so the seam header stays free of any registry dependency,
// preserving the flat topology.
using key_recognizer = std::function<bool(const key_path &)>;

// The argv source: maps `--a-b-c=v` flags onto the SAME keyspace every other
// source feeds, GOING THROUGH the runtime-virtual `source` seam (it is a
// non-document source emitting entries directly -- not a bespoke parallel path).
//
// Pull does two things in order, mirroring the locked design:
//   1. Syntactic mapping  -- normalize_arg turns each token into a (path, value)
//      via the `-` <-> `/` bijection. Bad syntax is a pull error.
//   2. Schema validation  -- the mapped path is checked against the schema-
//      dictated surface (the recognizer). Unknown paths are an error (strict) or
//      a logged store-as-string (lenient). This is where schema-as-authority
//      lives; segmentation stays simple.
//
// Its capability descriptor is honestly restrictive: a flag stream is flat (no
// nesting structure of its own -- the hierarchy comes from the path), carries no
// typed scalars, comments, or ordering guarantees. Like env, that makes it a real
// exerciser of feature degradation rather than a source that claims everything.
class argv_source final : public source
{
public:
    argv_source() = default;

    explicit argv_source(std::vector<std::string> args)
        : m_args(std::move(args))
    {
    }

    // Installs the schema-surface recognizer used for the validate-after step.
    // Without one every syntactically valid flag is accepted (mapping only).
    argv_source &recognize_with(key_recognizer recognizer)
    {
        m_recognizer = std::move(recognizer);
        return *this;
    }

    argv_source &policy(unknown_key_policy policy) noexcept
    {
        m_policy = policy;
        return *this;
    }

    // Routes degradation/lenient warnings. Defaults to the no-op sink.
    argv_source &log_to(log_sink &sink) noexcept
    {
        m_log = &sink;
        return *this;
    }

    [[nodiscard]] static capability_descriptor descriptor() noexcept
    {
        return capability_descriptor{};
    }

    [[nodiscard]] capability_descriptor capabilities() const override
    {
        return descriptor();
    }

    [[nodiscard]] source_result pull() override
    {
        source_batch batch;
        batch.entries.reserve(m_args.size());

        for(const std::string &token : m_args)
        {
            auto mapped = normalize_arg(token);
            if(!mapped)
                return unexpected(mapped.error());

            const key_path &path = mapped.value().key;

            const bool recognized = !m_recognizer || m_recognizer(path);
            if(!recognized)
            {
                if(m_policy == unknown_key_policy::strict)
                {
                    return unexpected(nucleus::format(
                        "unknown CLI flag '{}' maps to undeclared key '{}'",
                        token, path.str()));
                }
                if(m_log)
                {
                    m_log->log(log_level::warn, nucleus::format(
                        "unknown CLI flag '{}'; lenient mode -- stored as string at '{}'",
                        token, path.str()));
                }
            }

            // Owned values: the argv strings are copied, so the batch pins no
            // buffer.
            batch.entries.push_back(make_entry(
                path.str(), value::owned(mapped.value().value), descriptor()));
        }

        return batch;
    }

private:
    std::vector<std::string> m_args;
    key_recognizer m_recognizer;
    unknown_key_policy m_policy = unknown_key_policy::strict;
    log_sink *m_log = nullptr;
};

}

#endif
