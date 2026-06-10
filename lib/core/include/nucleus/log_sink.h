#ifndef HPP_GUARD_NUCLEUS_LOG_SINK_H
#define HPP_GUARD_NUCLEUS_LOG_SINK_H

#include <string>
#include <cstdint>
#include <ostream>
#include <utility>
#include <string_view>

namespace nucleus {

enum class log_level : std::uint8_t
{
    trace,
    debug,
    info,
    warn,
    error,
};

[[nodiscard]] constexpr std::string_view to_string(log_level level) noexcept
{
    switch(level)
    {
        case log_level::trace: return "trace";
        case log_level::debug: return "debug";
        case log_level::info:  return "info";
        case log_level::warn:  return "warn";
        case log_level::error: return "error";
    }
    return "unknown";
}

// The logging seam: a minimal level + message contract with a no-op default and
// zero dependency on any logging library. The message is already-formatted text
// (callers compose it with nucleus::format, the std::format vocabulary). A host
// injects a bridge to its real logger by subclassing or via the adapters below;
// the core only ever calls log() and never decides logging policy.
class log_sink
{
public:
    virtual ~log_sink() = default;

    virtual void log(log_level level, std::string_view message)
    {
        (void)level;
        (void)message;
    }
};

// Bridges the seam to any callable invocable as f(log_level, std::string_view) --
// the typical lambda a host injects to forward into its own logger.
template <typename Callable>
class log_sink_f final : public log_sink
{
public:
    explicit log_sink_f(Callable callable) : m_callable(std::move(callable)) {}

    void log(log_level level, std::string_view message) override
    {
        m_callable(level, message);
    }

private:
    Callable m_callable;
};

template <typename Callable>
log_sink_f(Callable) -> log_sink_f<Callable>;

// Bridges the seam to a std::ostream, prefixing each line with the level.
class log_sink_s final : public log_sink
{
public:
    explicit log_sink_s(std::ostream &stream) : m_stream(stream) {}

    void log(log_level level, std::string_view message) override
    {
        m_stream << '[' << to_string(level) << "] " << message << '\n';
    }

private:
    std::ostream &m_stream;
};

}

#endif
