#ifndef HPP_GUARD_NUCLEUS_ARGV_ARGV_SOURCE_H
#define HPP_GUARD_NUCLEUS_ARGV_ARGV_SOURCE_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/log_sink.h"
#include "nucleus/capability.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"

#include "nucleus/argv/cli_surface.h"
#include "nucleus/configuration_source/argv/key_recognizer.h"

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

// The argv source: maps `--a-b-c=v` flags onto the SAME keyspace every other
// source feeds (it is a non-document source emitting entries directly -- not a
// bespoke parallel path).
//
// Pull does two things in order, mirroring the locked design:
//   1. Syntactic mapping  -- normalize_arg turns each token into a (path, value)
//      via the delimiter <-> `/` bijection (delimiter `-` unless the host picks
//      another via delimit_with). Bad syntax is a pull error.
//   2. Schema validation  -- the mapped path is checked against the schema-
//      dictated surface (the recognizer). Unknown paths are an error (strict) or
//      a logged store-as-string (lenient). This is where schema-as-authority
//      lives; segmentation stays simple.
//
// Its capability descriptor declares nesting and duplicate_keys, mirroring
// runtime_source's rationale: the bijection genuinely addresses nested paths
// (the hierarchy comes from the flag's path, exactly as it comes from set()'s),
// and repeating a flag (`--tag=a --tag=b`) is the CLI idiom for collections --
// pull() emits one entry per token in order, so repeats compose. typed_scalars
// and comments stay undeclared: flag values are text, so typing degrades softly.
//
// Plain struct satisfying the source concept by duck typing.
class argv_source final
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

    // Picks the flag delimiter of the bijection. The emitter and the completion
    // generator must be handed the SAME delimiter, or the projected flag surface
    // drifts from what pull accepts.
    argv_source &delimit_with(cli_delimiter delimiter)
    {
        m_delimiter = std::move(delimiter);
        return *this;
    }

    // Anchors every flag at a fixed path prefix: with anchor `server`, `--host=x`
    // maps to `server/host`. EVERY flag is relative to the anchor -- a host whose
    // keyspace lives under one never-changing root drops it from the whole flag
    // surface. The emitter and the completion generator must share the anchor.
    argv_source &anchor_at(key_path anchor)
    {
        m_anchor = std::move(anchor);
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
        return capability_descriptor{capability::nesting,
                                     capability::duplicate_keys};
    }

    [[nodiscard]] capability_descriptor capabilities() const
    {
        return descriptor();
    }

    [[nodiscard]] configuration_source_result pull()
    {
        configuration_source_batch batch;
        batch.entries.reserve(m_args.size());

        for(const std::string &token : m_args)
        {
            auto mapped = normalize_arg(token, m_delimiter);
            if(!mapped)
                return unexpected(configuration_source_error{
                    errc::malformed_source, std::move(mapped).error()});

            key_path path = std::move(mapped.value().key);
            if(!m_anchor.empty())
                path = m_anchor.join(path);

            const bool recognized = !m_recognizer || m_recognizer(path);
            if(!recognized)
            {
                if(m_policy == unknown_key_policy::strict)
                {
                    return unexpected(configuration_source_error{
                        errc::schema_violation, nucleus::format(
                            "unknown CLI flag '{}' maps to undeclared key '{}'",
                            token, path.str())});
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
    cli_delimiter m_delimiter;
    key_path m_anchor;
    key_recognizer m_recognizer;
    unknown_key_policy m_policy = unknown_key_policy::strict;
    log_sink *m_log = nullptr;
};

}

#endif
