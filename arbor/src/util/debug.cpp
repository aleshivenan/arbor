#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>

#include <util/debug.hpp>
#include <util/ioutil.hpp>
#include <util/unwind.hpp>

namespace arb {
namespace util {

std::mutex global_debug_cerr_mutex;

bool abort_on_failed_assertion(
    const char* assertion,
    const char* file,
    int line,
    const char* func)
{
    // If libunwind is being used, make a file with a backtrace and print information
    // to stdcerr.
    backtrace().print();

    // Explicit flush, as we can't assume default buffering semantics on stderr/cerr,
    // and abort() might not flush streams.
    std::cerr << file << ':' << line << " " << func
              << ": Assertion `" << assertion << "' failed." << std::endl;
    std::abort();
    return false;
}

failed_assertion_handler_t global_failed_assertion_handler = abort_on_failed_assertion;

std::ostream& debug_emit_trace_leader(
    std::ostream& out,
    const char* file,
    int line,
    const char* varlist)
{
    iosfmt_guard guard(out);

    const char* leaf = std::strrchr(file, '/');
    out << (leaf?leaf+1:file) << ':' << line << " ";

    using namespace std::chrono;
    auto tstamp = system_clock::now().time_since_epoch();
    auto tstamp_usec = duration_cast<microseconds>(tstamp).count();

    out << std::right << '[';
    out << std::setw(11) << std::setfill('0') << (tstamp_usec/1000000) << '.';
    out << std::setw(6)  << std::setfill('0') << (tstamp_usec%1000000) << ']';

    if (varlist && *varlist) {
        out << ' ' << varlist << ": ";
    }
    return out;
}

} // namespace util
} // namespace arb
