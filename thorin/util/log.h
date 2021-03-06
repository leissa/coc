#ifndef THORIN_UTIL_LOG_H
#define THORIN_UTIL_LOG_H

#include <iomanip>
#include <iostream>
#include <cstdlib>
#include <ostream>
#include <sstream>

#include "thorin/util/debug.h"
#include "thorin/util/stream.h"

namespace thorin {

class Log {
    Log() = delete;
    Log(const Log&) = delete;
    Log& operator= (const Log&) = delete;

public:
    enum Level {
        Debug, Verbose, Info, Warn, Error,
    };

    static std::ostream& stream();
    static void set(Level min_level, std::ostream& stream, bool print_loc = true);
    static Level min_level();
    static void set_stream(std::ostream& stream);
    static void set_min_level(Level min_level);
    static void set_print_loc(bool print_loc);
    static std::string level2string(Level);
    static int level2color(Level);
    static std::string colorize(const std::string&, int);

    template<typename... Args>
    static void log(Level level, Loc loc, const char* fmt, Args... args) {
        if (Log::get_stream() && Log::get_min_level() <= level) {
            std::ostringstream oss;
            oss << loc;
            if (Log::get_print_loc())
                Log::stream() << colorize(level2string(level), level2color(level)) << ':'
                              << colorize(oss.str(), 7) << ": ";
            if (level == Debug)
                Log::stream() << "  ";
            streamf(Log::stream(), fmt, std::forward<Args>(args)...);
            Log::stream() << std::endl;
        }
    }

    template<typename... Args>
    [[noreturn]] static void error(Loc loc, const char* fmt, Args... args) {
        log(Error, loc, fmt, args...);
        std::abort();
    }

private:
    static std::ostream& get_stream();
    static Level get_min_level();
    static bool get_print_loc();

    static std::ostream* stream_;
    static Level min_level_;
    static bool print_loc_;
};

}

#define ELOG(...) thorin::Log::log(thorin::Log::Error,   Loc(__FILE__, __LINE__, -1), __VA_ARGS__)
#define WLOG(...) thorin::Log::log(thorin::Log::Warn,    Loc(__FILE__, __LINE__, -1), __VA_ARGS__)
#define ILOG(...) thorin::Log::log(thorin::Log::Info,    Loc(__FILE__, __LINE__, -1), __VA_ARGS__)
#define VLOG(...) thorin::Log::log(thorin::Log::Verbose, Loc(__FILE__, __LINE__, -1), __VA_ARGS__)
#ifndef NDEBUG
#define DLOG(...) thorin::Log::log(thorin::Log::Debug,   Loc(__FILE__, __LINE__, -1), __VA_ARGS__)
#else
#define DLOG(...) do {} while (false)
#endif

#endif
