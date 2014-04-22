#ifndef YRMCDS_TEST_SEMAPHORE_CLIENT_HPP
#define YRMCDS_TEST_SEMAPHORE_CLIENT_HPP

#include "../src/semaphore/semaphore.hpp"

#include <cybozu/util.hpp>
#include <cybozu/test.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace semaphore_client {

typedef std::uint32_t serial_t;
using yrmcds::semaphore::string_slice;

const std::size_t HEADER_SIZE = 12;

struct statistic {
    std::string name;
    std::string value;
};

class response {
public:
    std::size_t parse(const std::vector<char> buf) {
        if( buf.size() < HEADER_SIZE )
            return 0;
        std::memcpy(m_header, buf.data(), HEADER_SIZE);
        if( buf.size() < HEADER_SIZE + body_length() )
            return 0;
        m_body.assign(buf.begin() + HEADER_SIZE,
                      buf.begin() + HEADER_SIZE + body_length());
        return HEADER_SIZE + m_body.size();
    }

    std::uint8_t magic() const noexcept {
        return (uint8_t)m_header[0];
    }

    yrmcds::semaphore::command command() const noexcept {
        return (yrmcds::semaphore::command)m_header[1];
    }

    yrmcds::semaphore::status status() const noexcept {
        return (yrmcds::semaphore::status)m_header[2];
    }

    std::uint32_t body_length() const {
        uint32_t r;
        cybozu::ntoh(m_header + 4, r);
        return r;
    }

    const std::string name() const {
        if( command() == yrmcds::semaphore::command::Dump ) {
            uint16_t name_len;
            cybozu::ntoh(m_body.data() + 12, name_len);
            return std::string(m_body.data() + 14, name_len);
        }
        cybozu_assert(false);
        return "";
    }

    std::uint32_t opaque() const {
        uint32_t r;
        cybozu::ntoh(m_header + 8, r);
        return r;
    }

    std::uint32_t resources() const {
        cybozu_assert(m_body.size() >= 4);
        uint32_t r;
        cybozu::ntoh(m_body.data(), r);
        return r;
    }

    std::uint32_t available() const {
        return resources();
    }

    std::uint32_t maximum() const {
        cybozu_assert(m_body.size() >= 8);
        uint32_t r;
        cybozu::ntoh(m_body.data() + 4, r);
        return r;
    }

    std::uint32_t max_consumption() const {
        cybozu_assert(m_body.size() >= 12);
        uint32_t r;
        cybozu::ntoh(m_body.data() + 8, r);
        return r;
    }

    std::string message() const {
        return std::string(m_body.data(), m_body.size());
    }

    std::vector<statistic> stats() const {
        std::vector<statistic> s;
        const char* end = m_body.data() + m_body.size();
        for( const char* p = m_body.data(); p < end; ) {
            cybozu_assert(p + 4 <= end);
            std::uint16_t name_len, value_len;
            cybozu::ntoh(p, name_len);
            cybozu::ntoh(p + 2, value_len);
            cybozu_assert(p + 4 + name_len + value_len <= end);
            s.push_back({ std::string(p + 4, name_len),
                          std::string(p + 4 + name_len, value_len) });
            p += 4 + name_len + value_len;
        }
        return std::move(s);
    }

private:
    char m_header[HEADER_SIZE] = {};
    std::vector<char> m_body;
};

class client {
public:
    client(int fd): m_socket(fd), m_buffer(), m_recv_buffer(1<<16) {
        cybozu_assert( m_socket != -1 );
    }

    ~client() { ::close(m_socket); }

    bool recv(response& r) {
        while( true ) {
            std::size_t len = r.parse(m_buffer);
            if( len > 0 ) {
                m_buffer.erase(m_buffer.begin(), m_buffer.begin() + len);
                return true;
            }
            ssize_t n = ::recv(m_socket, m_recv_buffer.data(), m_recv_buffer.size(), 0);
            cybozu_assert( n != -1 );
            if( n == -1 || n == 0 ) {
                auto ecnd = std::system_category().default_error_condition(errno);
                std::cerr << "<client:recv> (" << ecnd.value() << ") " << ecnd.message() << std::endl;
                return false;
            }
            m_buffer.insert(m_buffer.end(), m_recv_buffer.begin(), m_recv_buffer.begin() + n);
        }
    }

    serial_t noop() {
        char header[HEADER_SIZE];
        fill_header(header, 0x00, 0, m_serial);
        ssize_t n = ::send(m_socket, header, HEADER_SIZE, 0);
        cybozu_assert( n == ssize_t(HEADER_SIZE) );
        return m_serial++;
    }

    serial_t get(const std::string& name) {
        char header[HEADER_SIZE];
        fill_header(header, 0x01, 2 + name.size(), m_serial);
        char body[2];
        cybozu::hton((uint16_t)name.size(), body);

        m_send_buffer.resize(HEADER_SIZE + sizeof(body) + name.size());
        std::memcpy(m_send_buffer.data(), header, HEADER_SIZE);
        std::memcpy(m_send_buffer.data() + HEADER_SIZE, body, sizeof(body));
        std::memcpy(m_send_buffer.data() + HEADER_SIZE + sizeof(body), name.data(), name.size());

        ssize_t n = ::send(m_socket, m_send_buffer.data(), m_send_buffer.size(), 0);
        cybozu_assert( n == (ssize_t)m_send_buffer.size() );
        return m_serial++;
    }

    serial_t acquire(const std::string& name, std::uint32_t resources, std::uint32_t initial) {
        char header[HEADER_SIZE];
        fill_header(header, 0x02, 10 + name.size(), m_serial);
        char body[10];
        cybozu::hton(resources, body);
        cybozu::hton(initial, body + 4);
        cybozu::hton((uint16_t)name.size(), body + 8);

        m_send_buffer.resize(HEADER_SIZE + sizeof(body) + name.size());
        std::memcpy(m_send_buffer.data(), header, HEADER_SIZE);
        std::memcpy(m_send_buffer.data() + HEADER_SIZE, body, sizeof(body));
        std::memcpy(m_send_buffer.data() + HEADER_SIZE + sizeof(body), name.data(), name.size());

        ssize_t n = ::send(m_socket, m_send_buffer.data(), m_send_buffer.size(), 0);
        cybozu_assert( n == (ssize_t)m_send_buffer.size() );
        return m_serial++;
    }

    serial_t release(const std::string& name, std::uint32_t resources) {
        char header[HEADER_SIZE];
        fill_header(header, 0x03, 6 + name.size(), m_serial);
        char body[6];
        cybozu::hton(resources, body);
        cybozu::hton((uint16_t)name.size(), body + 4);

        m_send_buffer.resize(HEADER_SIZE + sizeof(body) + name.size());
        std::memcpy(m_send_buffer.data(), header, HEADER_SIZE);
        std::memcpy(m_send_buffer.data() + HEADER_SIZE, body, sizeof(body));
        std::memcpy(m_send_buffer.data() + HEADER_SIZE + sizeof(body), name.data(), name.size());

        ssize_t n = ::send(m_socket, m_send_buffer.data(), m_send_buffer.size(), 0);
        cybozu_assert( n == (ssize_t)m_send_buffer.size() );
        return m_serial++;
    }

    serial_t stats() {
        char header[HEADER_SIZE];
        fill_header(header, 0x10, 0, m_serial);
        ssize_t n = ::send(m_socket, header, HEADER_SIZE, 0);
        cybozu_assert( n == ssize_t(HEADER_SIZE) );
        return m_serial++;
    }

    serial_t dump() {
        char header[HEADER_SIZE];
        fill_header(header, 0x11, 0, m_serial);
        ssize_t n = ::send(m_socket, header, HEADER_SIZE, 0);
        cybozu_assert( n == ssize_t(HEADER_SIZE) );
        return m_serial++;
    }

private:
    void fill_header(char* header, uint8_t opcode, uint32_t body_length, uint32_t opaque) {
        header[0] = 0x90;
        header[1] = opcode;
        header[2] = 0;
        header[3] = 0;
        cybozu::hton(body_length, header + 4);
        cybozu::hton(opaque, header + 8);
    }

    int m_socket;
    serial_t m_serial;
    std::vector<char> m_buffer;
    std::vector<char> m_recv_buffer;
    std::vector<char> m_send_buffer;
};

} // namespace semaphore_client

#endif // YRMCDS_TEST_SEMAPHORE_CLIENT_HPP
