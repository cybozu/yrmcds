// (C) 2013 Cybozu.

#include "logger.hpp"
#include "util.hpp"

#include <ctime>
#include <stdexcept>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <cstddef>

namespace cybozu {

void logstream::add_prefix(const char* level) {
    char buf[64];
    std::time_t t = std::time(nullptr);
    std::tm ct;
    // beware that std::gmtime is not thread-safe.
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %T ", ::gmtime_r(&t, &ct));
    *m_os << buf << level << " ";
}

void logstream::output() {
    if( m_os.get() == nullptr ) return;
    *m_os << "\n";
    logger::instance().log(m_os->str());
    m_os.reset(nullptr);
}

std::atomic<severity> logger::_threshold{ severity::info };

void logger::open_nolock(const std::string& path) {
    if( m_fd != -1 && m_fd != STDERR_FILENO )
        throw std::logic_error("<logger::open> double open.");
    m_fd = ::open(path.c_str(), O_WRONLY|O_APPEND|O_CREAT, 0644);
    if( m_fd == -1 ) {
        m_fd = STDERR_FILENO;
        throw_unix_error(errno, ("open(" + path + ")").c_str());
    }
}

void logger::log(const std::string& msg) {
    lock_guard g(m_lock);
    std::size_t to_write = msg.size();
    const char* ptr = msg.data();
    while( to_write != 0 ) {
        ssize_t n = ::write(m_fd, ptr, to_write);
        if( n == -1 ) {
            if( errno == EAGAIN || errno == EINTR ) continue;
            throw_unix_error(errno, "<logger::log> write");
        }
        ptr += n;
        to_write -= n;
    }
}

} // namespace cybozu
