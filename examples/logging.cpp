#include "nucleus/log_sink.h"

#include "nucleus/source/argv/argv_source.h"

#include "nucleus/keyspace/key_path.h"

#include <vector>
#include <iostream>
#include <string>
#include <string_view>

// logging: inject a custom log_sink to redirect the engine's diagnostics.
//
// The log_sink seam is a minimal level + message contract with a no-op default
// and zero dependency on any logging library. A host bridges it to its real
// logger by subclassing log_sink, or by wrapping a callable with log_sink_f, or
// an ostream with log_sink_s. Here a source runs in lenient mode and warns about
// an unknown flag through a custom sink that prefixes and counts the messages.

namespace {

// A bridge to a host's own logger. In a real program log() would forward into
// spdlog / a file / a ring buffer; here it prints with a custom prefix and keeps
// a count so we can prove the engine actually routed through it.
class counting_sink final : public nucleus::log_sink
{
public:
    void log(nucleus::log_level level, std::string_view message) override
    {
        ++m_count;
        std::cout << "[app/" << nucleus::to_string(level) << "] " << message << '\n';
    }

    [[nodiscard]] int count() const noexcept { return m_count; }

private:
    int m_count = 0;
};

} // namespace

int main()
{
    counting_sink sink;

    // The argv source recognizes only schema-declared flags. Here the recognizer
    // declares just `service/name`, so `--service-mode` maps to an undeclared key.
    // In lenient mode the source warns through the sink and stores the value
    // anyway instead of failing the pull.
    nucleus::argv_source args(std::vector<std::string>{
        "--service-name=edge", "--service-mode=fast"});
    args.recognize_with([](const nucleus::key_path &path) {
            return path.str() == "service/name";
        })
        .policy(nucleus::unknown_key_policy::lenient)
        .log_to(sink);

    auto pulled = args.pull();
    if(!pulled)
    {
        std::cerr << "pull failed: " << pulled.error() << '\n';
        return 1;
    }

    std::cout << "the engine routed " << sink.count()
              << " message(s) through the injected sink\n";

    return 0;
}
