// (C) 2014 Cybozu.

#include "semaphore.hpp"
#include "stats.hpp"

#include <cybozu/util.hpp>

#include <string>
#include <vector>

namespace {

const std::size_t HEADER_SIZE = 12;
const std::uint8_t REQUEST_MAGIC  = 0x90;
const std::uint8_t RESPONSE_MAGIC = 0x91;
const char STATUS_OK[]                     = "No error";
const char STATUS_NOT_FOUND[]              = "Not found";
const char STATUS_INVALID[]                = "Invalid arguments";
const char STATUS_RESOURCE_NOT_AVAILABLE[] = "Resource not available";
const char STATUS_NOT_ACQUIRED[]           = "Not acquired";
const char STATUS_UNKNOWN_COMMAND[]        = "Unknown command";
const char STATUS_OUT_OF_MEMORY[]          = "OutOfMemory";

void add_stat(std::vector<char>& body, const std::string& name,
              const std::string& value) {
    char buffer[4];
    cybozu::hton((uint16_t)name.size(), buffer);
    cybozu::hton((uint16_t)value.size(), buffer+2);
    body.insert(body.end(), buffer, buffer + 4);
    body.insert(body.end(), name.begin(), name.end());
    body.insert(body.end(), value.begin(), value.end());
}

template<typename T>
void add_stat_atomic(std::vector<char>& body, const std::string& name,
                     const std::atomic<T>& value) {
    add_stat(body, name, std::to_string(value.load(std::memory_order_relaxed)));
}

void add_stat_op(std::vector<char>& body, const std::string& name,
                 yrmcds::semaphore::command c) {
    const auto& n = yrmcds::semaphore::g_stats.ops[(std::uint8_t)c];
    add_stat(body, name, std::to_string(n.load(std::memory_order_relaxed)));
}

} // namespace anonymous

namespace yrmcds { namespace semaphore {

void request::parse(const char* p, std::size_t len) noexcept {
    if( len < HEADER_SIZE ) return;  // incomplete
    std::uint32_t body_length;
    cybozu::ntoh(p + 4, body_length);
    if( len < HEADER_SIZE + body_length ) return;  // incomplete

    m_command = (semaphore::command)*(const uint8_t*)(p+1);
    m_flags = *(const uint8_t*)(p+2);
    m_body_length = body_length;
    m_opaque = p + 8;

    m_request_length = HEADER_SIZE + body_length;
    const char* body = p + HEADER_SIZE;
    std::uint16_t name_len;

    if( *(const std::uint8_t*)p != REQUEST_MAGIC )
        return;  // invalid

    switch( m_command ) {
    case semaphore::command::Noop:
    case semaphore::command::Stats:
        // no body
        break;

    case semaphore::command::Get:
        if( body_length < 2 )
            return;     // invalid
        cybozu::ntoh(body, name_len);
        if( name_len == 0 || 2U + name_len > body_length )
            return;     // invalid
        m_name.len = name_len;
        m_name.p = body + 2;
        break;

    case semaphore::command::Acquire:
        if( body_length < 10 )
            return;     // invalid
        cybozu::ntoh(body, m_resources);
        if( m_resources == 0 )
            return;     // invalid
        cybozu::ntoh(body + 4, m_initial);
        if( m_initial < m_resources )
            return;
        cybozu::ntoh(body + 8, name_len);
        if( name_len == 0 || 10U + name_len > body_length )
            return;     // invalid
        m_name.len = name_len;
        m_name.p = body + 10;
        break;

    case semaphore::command::Release:
        if( body_length < 6 )
            return;     // invalid
        cybozu::ntoh(body, m_resources);
        cybozu::ntoh(body + 4, name_len);
        if( name_len == 0 || 6U + name_len > body_length )
            return;     // invalid
        m_name.len = name_len;
        m_name.p = body + 6;
        break;

    default:
        m_command = semaphore::command::Unknown;
        m_status = semaphore::status::UnknownCommand;
        return;
    }

    m_status = semaphore::status::OK;
}

inline void response::fill_header(char* header, semaphore::status status,
                                  std::uint32_t body_length) {
    header[0] = (char)RESPONSE_MAGIC;
    header[1] = (char)m_request.command();
    header[2] = (char)status;
    header[3] = 0;
    cybozu::hton(body_length, header + 4);
    std::memcpy(header + 8, m_request.opaque(), 4);
}

void response::success() {
    char header[HEADER_SIZE];
    fill_header(header, semaphore::status::OK, 0);
    m_socket.send(header, HEADER_SIZE, true);
}

void response::error(semaphore::status status) {
    switch( status ) {
    case semaphore::status::NotFound:
        send_error(status, STATUS_NOT_FOUND,
                   sizeof(STATUS_NOT_FOUND) - 1);
        break;
    case semaphore::status::Invalid:
        send_error(status, STATUS_INVALID,
                   sizeof(STATUS_INVALID) - 1);
        break;
    case semaphore::status::ResourceNotAvailable:
        send_error(status, STATUS_RESOURCE_NOT_AVAILABLE,
                   sizeof(STATUS_RESOURCE_NOT_AVAILABLE) - 1);
        break;
    case semaphore::status::NotAcquired:
        send_error(status, STATUS_NOT_ACQUIRED,
                   sizeof(STATUS_NOT_ACQUIRED) - 1);
        break;
    case semaphore::status::UnknownCommand:
        send_error(status, STATUS_UNKNOWN_COMMAND,
                   sizeof(STATUS_UNKNOWN_COMMAND) - 1);
        break;
    case semaphore::status::OutOfMemory:
        send_error(status, STATUS_OUT_OF_MEMORY,
                   sizeof(STATUS_OUT_OF_MEMORY) - 1);
        break;
    default:
        cybozu::dump_stack();
        throw std::logic_error("<semaphore::response::error> invalid status: " +
                               std::to_string((std::uint8_t)status));
    }
}

void response::get(std::uint32_t available) {
    char header[HEADER_SIZE];
    fill_header(header, semaphore::status::OK, sizeof(available));
    char body[4];
    cybozu::hton(available, body);
    cybozu::tcp_socket::iovec iov[] = {
        {header, HEADER_SIZE},
        {body, 4},
    };
    m_socket.sendv(iov, 2, true);
}

void response::acquire(std::uint32_t resources) {
    // internally same as get()
    get(resources);
}

void response::stats() {
    const statistics& s = g_stats;
    std::vector<char> body;
    add_stat_atomic(body, "objects", s.objects);
    add_stat_atomic(body, "total_objects", s.total_objects);
    add_stat_atomic(body, "used_memory", s.used_memory);
    add_stat_atomic(body, "conflicts", s.conflicts);
    add_stat_atomic(body, "gc_count", s.gc_count);
    add_stat_atomic(body, "last_gc_elapsed", s.last_gc_elapsed);
    add_stat_atomic(body, "total_gc_elapsed", s.total_gc_elapsed);
    add_stat_atomic(body, "curr_connections", s.curr_connections);
    add_stat_atomic(body, "total_connections", s.total_connections);
    add_stat_op(body, "command:noop", semaphore::command::Noop);
    add_stat_op(body, "command:get", semaphore::command::Get);
    add_stat_op(body, "command:acquire", semaphore::command::Acquire);
    add_stat_op(body, "command:release", semaphore::command::Release);
    add_stat_op(body, "command:stats", semaphore::command::Stats);

    char header[HEADER_SIZE];
    fill_header(header, semaphore::status::OK, body.size());
    cybozu::tcp_socket::iovec iov[] = {
        {header, HEADER_SIZE},
        {body.data(), body.size()},
    };
    m_socket.sendv(iov, 2, true);
}

void response::send_error(semaphore::status status, const char* message,
                          std::size_t length) {
    char header[HEADER_SIZE];
    fill_header(header, status, length);
    cybozu::tcp_socket::iovec iov[] = {
        {header, HEADER_SIZE},
        {message, length},
    };
    m_socket.sendv(iov, 2, true);
}

}} // namespace yrmcds::semaphore
