// Temporary file to store object data.
// (C) 2013 Cybozu.

#ifndef YRMCDS_TEMPFILE_HPP
#define YRMCDS_TEMPFILE_HPP

#include "config.hpp"

#include <cybozu/dynbuf.hpp>
#include <cybozu/util.hpp>

#include <fcntl.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace yrmcds {

// A helper function for <tempfile>.
inline int mkstemp_wrap(const std::string& tempdir) {
    std::string tmpl = tempdir + "/XXXXXX\0";
    int fd = ::mkostemp(&(tmpl[0]), O_CLOEXEC);
    if( fd == -1 )
        cybozu::throw_unix_error(errno, "mkstemp");
    ::unlink(tmpl.data());
    return fd;
}


// A temporary file abstraction.
class tempfile {
public:
    tempfile(): m_fd(mkstemp_wrap(g_config.tempdir())) {}
    tempfile(const tempfile&) = delete;
    tempfile& operator=(const tempfile&) = delete;
    tempfile(tempfile&& rhs) = delete;
    tempfile& operator=(tempfile&&) = delete;
    ~tempfile() { ::close(m_fd); }

    // Append data to the end of the current contents.
    // @p    Pointer to the data.
    // @len  Length of the data
    void write(const char* p, std::size_t len);

    // Clear the current file contents.
    void clear();

    // Return the open file descriptor.
    int fileno() const {
        return m_fd;
    }

    // Read the file contents.
    // @buf  Storage for the file contents.
    //
    // Read the file contents and append them to `buf`.
    void read_contents(cybozu::dynbuf& buf) const;

    std::size_t length() const noexcept {
        return m_length;
    }

private:
    const int m_fd;
    std::size_t m_length = 0;
};

} // namespace yrmcds

#endif // YRMCDS_TEMPFILE_HPP
