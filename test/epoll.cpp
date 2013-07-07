// test if epoll can receive hangup at clients.

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>
#include <system_error>
#include <cerrno>
#include <sys/epoll.h>
#include <thread>
#include <iostream>
#include <unistd.h>
#include <vector>

int create_socket(const struct addrinfo& ai) {
    int s = socket(ai.ai_family, ai.ai_socktype, ai.ai_protocol);
    if( s == -1 )
        throw std::system_error(errno, std::system_category(), "socket");
    int enable = 1;
    if( setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1 )
        throw std::system_error(errno, std::system_category(), "setsocketopt");
    if( bind(s, ai.ai_addr, ai.ai_addrlen) == -1 )
        throw std::system_error(errno, std::system_category(), "bind");
    if( listen(s, 10) == -1 )
        throw std::system_error(errno, std::system_category(), "listen");
    return s;
}

int main(int argc, char** argv) {
    if( argc != 2 ) {
        std::cout << "Usage: epoll PORT" << std::endl;
        return 0;
    }
    struct addrinfo hint;
    std::memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = 0;
    hint.ai_flags = AI_PASSIVE;
    struct addrinfo *res;
    int e = getaddrinfo(NULL, argv[1], &hint, &res);
    if( e == EAI_SYSTEM ) {
        throw std::system_error(errno, std::system_category(), "getaddrinfo");
    } else if( e != 0 ) {
        throw std::runtime_error(std::string("getaddrinfo: ") +
                                 gai_strerror(e));
    }
    const struct addrinfo* p;
    int s;
    for( p = res; p != nullptr; p = p->ai_next )
        if( p->ai_family == AF_INET6 ) break;
    if( p != nullptr ) {
        s = create_socket(*p);
    } else {
        for( p = res; p != nullptr; p = p->ai_next )
            if( p->ai_family == AF_INET ) break;
        if( p != nullptr ) {
            s = create_socket(*p);
        } else {
            throw std::runtime_error("No address to bind.");
        }
    }
    struct sockaddr addr;
    socklen_t slen = sizeof(addr);
    int clientfd = accept4(s, &addr, &slen, SOCK_NONBLOCK);
    if( clientfd == -1 )
        throw std::system_error(errno, std::system_category(), "accept4");
    close(s);

    int efd = epoll_create(10);
    if( efd == -1 )
        throw std::system_error(errno, std::system_category(), "epoll_create");
    struct epoll_event ev;
    ev.events = EPOLLIN|EPOLLET;
    ev.data.fd = clientfd;
    if( epoll_ctl(efd, EPOLL_CTL_ADD, clientfd, &ev) == -1 )
        throw std::system_error(errno, std::system_category(), "epoll_ctl");

    std::vector<char> buf(1 << 20);
    struct epoll_event events[10];
    while( true ) {
        int n = epoll_wait(efd, events, 10, -1);
        for(int i = 0; i < n; ++i ) {
            if( events[i].events & EPOLLIN ) {
                std::cout << "EPOLLIN" << std::endl;
                while( true ) {
                    if( recv(events[i].data.fd, buf.data(), buf.size(), 0) == -1 )
                        break;
                }
            }
            if( events[i].events & EPOLLHUP ) {
                std::cout << "EPOLLHUP" << std::endl;
                break;
            }
            if( events[i].events & EPOLLERR ) {
                std::cout << "EPOLLHUP" << std::endl;
            }
        }
    }
    return 0;
}
