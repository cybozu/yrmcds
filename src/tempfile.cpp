// (C) 2013 Cybozu.

#include "tempfile.hpp"

#include <stdexcept>
#include <sys/types.h>
#include <thread>

namespace yrmcds {

void tempfile::write(const char* p, std::size_t len) {
    while( len != 0 ) {
        ssize_t n = ::write(m_fd, p, len);
        if( n == -1 )
            cybozu::throw_unix_error(errno, "write");
        p += n;
        len -= n;
        m_length += n;
    }
}

void tempfile::clear() {
    if( lseek(m_fd, 0, SEEK_SET) == (off_t)-1 )
        cybozu::throw_unix_error(errno, "lseek");
    if( ftruncate(m_fd, 0) == -1 )
        cybozu::throw_unix_error(errno, "ftruncate");
    m_length = 0;
}

void tempfile::read_contents(cybozu::dynbuf& buf) const {
    char* p = buf.prepare(m_length);

    std::size_t to_read = m_length;
    while( to_read != 0 ) {
        ssize_t n = pread(m_fd, p, to_read, m_length - to_read);
        if( n == -1 )
            cybozu::throw_unix_error(errno, "pread");
        if( n == 0 )
            throw std::runtime_error("<tempfile::read_contents> unexpected EOF.");
        p += n;
        to_read -= n;
        buf.consume(n);
    }
}

} // namespace yrmcds
