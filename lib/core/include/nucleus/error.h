#ifndef HPP_GUARD_NUCLEUS_ERROR_H
#define HPP_GUARD_NUCLEUS_ERROR_H

#include <string>
#include <ostream>
#include <string_view>

namespace nucleus {

// The machine-readable identity of every failure the library reports. The codes
// follow the load pipeline (source -> inheritance -> gate -> fold -> tokens ->
// selection -> schema -> conversion), then registration, then typed reads. A
// host branches on the code; the human detail travels in error::message.
enum class errc
{
    unreadable_source,
    malformed_source,
    invalid_inheritance,
    unmet_capability,
    layering_violation,
    unresolved_token,
    invalid_selection,
    schema_violation,
    failed_conversion,
    rejected_registration,
    sealed_builder,
    absent_key,
    index_required,
    missing_converter,
    mismatched_type,
};

[[nodiscard]] constexpr std::string_view to_string(errc code) noexcept
{
    switch(code)
    {
        case errc::unreadable_source:     return "unreadable_source";
        case errc::malformed_source:      return "malformed_source";
        case errc::invalid_inheritance:   return "invalid_inheritance";
        case errc::unmet_capability:      return "unmet_capability";
        case errc::layering_violation:    return "layering_violation";
        case errc::unresolved_token:      return "unresolved_token";
        case errc::invalid_selection:     return "invalid_selection";
        case errc::schema_violation:      return "schema_violation";
        case errc::failed_conversion:     return "failed_conversion";
        case errc::rejected_registration: return "rejected_registration";
        case errc::sealed_builder:        return "sealed_builder";
        case errc::absent_key:            return "absent_key";
        case errc::index_required:        return "index_required";
        case errc::missing_converter:     return "missing_converter";
        case errc::mismatched_type:       return "mismatched_type";
    }
    return "unknown";
}

// The error payload of every public result channel: a code for programs and a
// verbatim human-readable reason. Host-supplied converters and registration
// policies traffic in plain message strings; the engine attaches the code at
// the seam where the failure class is known.
struct error
{
    errc        code;
    std::string message;

    [[nodiscard]] friend bool operator==(const error &, const error &) = default;
};

[[nodiscard]] inline std::string to_string(const error &e)
{
    std::string out(to_string(e.code));
    out += ": ";
    out += e.message;
    return out;
}

inline std::ostream &operator<<(std::ostream &os, const error &e)
{
    return os << to_string(e.code) << ": " << e.message;
}

}

#endif
