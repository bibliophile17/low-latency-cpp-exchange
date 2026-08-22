// Lightweight structured logging.
//
// Deliberately NOT used inside MatchingEngine::process()'s hot path
// (matching itself) -- see docs/performance.md. Logging involves
// formatting and (depending on sink) I/O, both of which have unbounded
// or high latency compared to the matching operations themselves.
// Gateway/network code and startup/shutdown paths log freely; the
// per-order matching logic does not.
#pragma once

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string_view>

namespace exchange::logging {

enum class Level : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_level(Level level) { min_level_ = level; }

    void log(Level level, std::string_view component, std::string_view message) {
        if (level < min_level_) return;
        auto now = std::chrono::system_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

        std::lock_guard<std::mutex> lock(mutex_); // logging is not latency-critical; a mutex here is fine
        std::fprintf(stderr, "[%lld] [%s] [%s] %.*s\n",
                     static_cast<long long>(us),
                     level_name(level),
                     std::string(component).c_str(),
                     static_cast<int>(message.size()), message.data());
    }

private:
    static const char* level_name(Level level) {
        switch (level) {
            case Level::Debug: return "DEBUG";
            case Level::Info: return "INFO";
            case Level::Warn: return "WARN";
            case Level::Error: return "ERROR";
        }
        return "?";
    }

    Level min_level_ = Level::Info;
    std::mutex mutex_;
};

inline void log_debug(std::string_view component, std::string_view message) {
    Logger::instance().log(Level::Debug, component, message);
}
inline void log_info(std::string_view component, std::string_view message) {
    Logger::instance().log(Level::Info, component, message);
}
inline void log_warn(std::string_view component, std::string_view message) {
    Logger::instance().log(Level::Warn, component, message);
}
inline void log_error(std::string_view component, std::string_view message) {
    Logger::instance().log(Level::Error, component, message);
}

} // namespace exchange::logging
