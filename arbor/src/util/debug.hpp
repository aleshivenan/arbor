#pragma once

#include <iostream>
#include <sstream>
#include <mutex>
#include <utility>

#include <threading/threading.hpp>
#include "unwind.hpp"

namespace arb {
namespace util {

constexpr inline bool is_debug_mode() {
#ifndef NDEBUG
    return true;
#else
    return false;
#endif
}
using failed_assertion_handler_t =
    bool (*)(const char* assertion, const char* file, int line, const char* func);

bool abort_on_failed_assertion(const char* assertion, const char* file, int line, const char* func);
inline bool ignore_failed_assertion(const char*, const char*, int, const char*) {
    return false;
}

// defaults to abort_on_failed_assertion;
extern failed_assertion_handler_t global_failed_assertion_handler;

std::ostream& debug_emit_trace_leader(std::ostream& out, const char* file, int line, const char* varlist);

inline void debug_emit(std::ostream& out) {
    out << "\n";
}

template <typename Head, typename... Tail>
void debug_emit(std::ostream& out, const Head& head, const Tail&... tail) {
    out << head;
    if (sizeof...(tail)) {
        out << ", ";
    }
    debug_emit(out, tail...);
}

extern std::mutex global_debug_cerr_mutex;

template <typename... Args>
void debug_emit_trace(const char* file, int line, const char* varlist, const Args&... args) {
    if (arb::threading::multithreaded()) {
        std::stringstream buffer;
        buffer.precision(17);

        debug_emit_trace_leader(buffer, file, line, varlist);
        debug_emit(buffer, args...);

        std::lock_guard<std::mutex> guard(global_debug_cerr_mutex);
        std::cerr << buffer.rdbuf();
        std::cerr.flush();
    }
    else {
        debug_emit_trace_leader(std::cerr, file, line, varlist);
        debug_emit(std::cerr, args...);
        std::cerr.flush();
    }
}

namespace impl {
    template <typename Seq, typename Separator>
    struct sepval {
        const Seq& seq;
        Separator sep;

        sepval(const Seq& seq, Separator sep): seq(seq), sep(std::move(sep)) {}

        friend std::ostream& operator<<(std::ostream& out, const sepval& sv) {
            bool emitsep = false;
            for (const auto& v: sv.seq) {
                if (emitsep) out << sv.sep;
                emitsep = true;
                out << v;
            }
            return out;
        }
    };
}

// Wrap a sequence or container of values so that they can be printed
// to an `std::ostream` with the elements separated by the supplied 
// separator.
template <typename Seq, typename Separator>
impl::sepval<Seq, Separator> sepval(const Seq& seq, Separator sep) {
    return impl::sepval<Seq, Separator>(seq, std::move(sep));
}

} // namespace util
} // namespace arb

#ifdef ARB_HAVE_TRACE
    #define TRACE(vars...) arb::util::debug_emit_trace(__FILE__, __LINE__, #vars, ##vars)
#else
    #define TRACE(...)
#endif

#ifdef ARB_HAVE_ASSERTIONS
    #ifdef __GNUC__
        #define DEBUG_FUNCTION_NAME __PRETTY_FUNCTION__
    #else
        #define DEBUG_FUNCTION_NAME __func__
    #endif

    #define EXPECTS(condition) \
       (void)((condition) || \
       arb::util::global_failed_assertion_handler(#condition, __FILE__, __LINE__, DEBUG_FUNCTION_NAME))
#else
    #define EXPECTS(condition) \
       (void)(false && (condition))
#endif // def ARB_HAVE_ASSERTIONS
