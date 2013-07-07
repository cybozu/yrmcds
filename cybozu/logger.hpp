// Thread-safe logger.
// (C) 2013 Cybozu.

#ifndef CYBOZU_LOGGER_HPP
#define CYBOZU_LOGGER_HPP

#include <memory>
#include <sstream>
#include <string>
#include <atomic>
#include <mutex>
#include <unistd.h>

namespace cybozu {

// Severity used for <logger>.
enum class severity {error, warning, info, debug};


// Output stream to the logger.
//
// Just like other output streams, you can output a variety of objects
// by `<<` operator.  The log is written in a line from within the
// destructor.
class logstream {
    const bool m_valid;
    std::unique_ptr<std::ostringstream> m_os;

    void add_prefix(const char* level);

public:
    logstream(bool valid_, const char* level):
        m_valid(valid_), m_os(valid_ ? new std::ostringstream() : nullptr) {
        if( valid_ ) add_prefix(level);
    }
    logstream(logstream&& rhs) noexcept:
        m_valid(rhs.m_valid), m_os(std::move(rhs.m_os)) {}
    logstream(const logstream&) = delete;
    logstream& operator=(const logstream&) = delete;
    ~logstream() {
        if( m_valid ) output();
    }

    // `true` if this stream is valid for logging.
    bool valid() const { return m_valid; }
    // Output the log line.  The stream gets invalidated.
    void output();

    template<typename T>
    logstream& operator<< (const T& arg) {
        if( m_valid ) *m_os << arg;
        return *this;
    }
};


// A thread-safe logger.
//
// A simple thread-safe logger.  By default, this logger outputs
// logs to the standard error.
class logger {
    int m_fd;
    std::string m_path;
    mutable std::mutex m_lock;
    typedef std::lock_guard<std::mutex> lock_guard;

    static std::atomic<severity> _threshold;

    logger(): m_fd(STDERR_FILENO), m_path("") {}
    void open_nolock(const std::string& path);
    void close_nolock() {
        if( m_fd == -1 || m_fd == STDERR_FILENO ) return;
        fsync(m_fd);
        ::close(m_fd);
        m_fd = -1;
    }

public:
    // Return the singleton.
    //
    // @return <logger> object.
    static logger& instance() {
        static logger l;
        return l;
    }

    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;
    // fsync and close the log file.
    ~logger() {
        close_nolock();
    }

    static severity threshold() {
        return _threshold.load(std::memory_order_relaxed);
    }
    static void set_threshold(severity new_threshold) {
        _threshold.store(new_threshold, std::memory_order_relaxed);
    }

    void open(const std::string& path) {
        lock_guard g(m_lock);
        m_path = path;
        open_nolock(path);
    }
    void reopen() {
        lock_guard g(m_lock);
        if( m_fd == STDERR_FILENO ) return;
        close_nolock();
        open_nolock(m_path);
    }
    void close() {
        lock_guard g(m_lock);
        close_nolock();
    }
    void log(const std::string& msg);

    static logstream error() {
        return logstream(threshold() >= severity::error, "ERROR");
    }
    static logstream warning() {
        return logstream(threshold() >= severity::warning, "WARN");
    }
    static logstream info() {
        return logstream(threshold() >= severity::info, "INFO");
    }
    static logstream debug() {
        return logstream(threshold() >= severity::debug, "DEBUG");
    }
};

} // namespace cybozu

#endif // CYBOZU_LOGGER_HPP
