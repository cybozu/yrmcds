// semaphore server-side protocol.
// (C) 2014 Cybozu.

#ifndef YRMCDS_SEMAPHORE_SEMAPHORE_HPP
#define YRMCDS_SEMAPHORE_SEMAPHORE_HPP

#include <cybozu/config_parser.hpp>
#include <cybozu/tcp.hpp>

namespace yrmcds { namespace semaphore {

enum class command: std::uint8_t {
    Noop            = 0x00,
    Get             = 0x01,
    Acquire         = 0x02,
    Release         = 0x03,
    Stats           = 0x10,
    Dump            = 0x11,

    Unknown,
    END_OF_COMMAND
};

enum class status: std::uint8_t {
    OK                      = 0x00,
    NotFound                = 0x01,
    Invalid                 = 0x04,
    ResourceNotAvailable    = 0x21,
    NotAcquired             = 0x22,
    UnknownCommand          = 0x81,
    OutOfMemory             = 0x82,
};

struct string_slice {
    const char* p = nullptr;
    std::size_t len = 0;
    string_slice() noexcept {}
    string_slice(const char* p, std::size_t len) noexcept: p(p), len(len) {}
};

class request final {
public:
    request(const char* p, std::size_t len) {
        if( p == nullptr )
            throw std::logic_error("<semaphore::request> `p` must not be nullptr");
        if( len == 0 )
            throw std::logic_error("<semaphore::request> `len` must not be zero");
        parse(p, len);
    }

    std::size_t length() const noexcept { return m_request_length; }
    semaphore::command command() const noexcept { return m_command; }
    std::uint8_t flags() const noexcept { return m_flags; }
    std::uint32_t body_length() const noexcept { return m_body_length; }
    const char* opaque() const noexcept { return m_opaque; }
    std::uint32_t resources() const noexcept { return m_resources; }
    std::uint32_t maximum() const noexcept { return m_maximum; }
    string_slice name() const noexcept { return m_name; }
    semaphore::status status() const noexcept { return m_status; }

private:
    void parse(const char* p, std::size_t len) noexcept;

    std::size_t m_request_length = 0;
    semaphore::command m_command = semaphore::command::Unknown;
    std::uint8_t m_flags = 0;
    std::uint32_t m_body_length = 0;
    const char* m_opaque = nullptr;
    std::uint32_t m_resources = 0;
    std::uint32_t m_maximum = 0;
    string_slice m_name;
    semaphore::status m_status = semaphore::status::Invalid;
};

class response final {
public:
    response(cybozu::tcp_socket& socket, const request& request):
        m_socket(socket), m_request(request) {}

    void success();
    void error(semaphore::status status);
    void get(std::uint32_t consumption);
    void acquire(std::uint32_t resources);
    void stats();
    void dump(const char* name, std::uint16_t name_len,
              std::uint32_t consumption, std::uint32_t max_conumption);

private:
    void fill_header(char* header, semaphore::status status,
                     std::uint32_t body_length);
    void send_error(semaphore::status status, const char* message,
                    std::size_t length);

    cybozu::tcp_socket& m_socket;
    const request& m_request;
};

}} // namespace yrmcds::semaphore

#endif // YRMCDS_SEMAPHORE_SEMAPHORE_HPP
