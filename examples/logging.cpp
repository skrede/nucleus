// logging: bridge the log_sink seam to a host logger.
//
// log_sink is a minimal level + message contract, no-op by default. A host
// bridges it three ways: wrap a callable with log_sink_f, wrap an ostream with
// log_sink_s, or subclass log_sink. Here a lenient argv source warns through one.

#include "nucleus/log_sink.h"

#include "nucleus/source/argv/argv_source.h"

#include "nucleus/keyspace/key_path.h"

#include <vector>
#include <string>
#include <iostream>
#include <string_view>

int main()
{
    int warnings = 0;

    // A callable bridged into the seam with log_sink_f -- no subclass needed.
    auto sink = nucleus::log_sink_f(
        [&warnings](nucleus::log_level level, std::string_view message) {
            ++warnings;
            std::cout << "[app/" << nucleus::to_string(level) << "] " << message << '\n';
        });

    nucleus::argv_source args(
        std::vector<std::string>{"--service-name=edge", "--service-mode=fast"});
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

    std::cout << "routed " << warnings << " warning(s) through the sink\n";
    return 0;
}
