#include "AIOxx/net_fd.hpp"

#include <cstring>
#include <fcntl.h>
#include <netdb.h>

namespace AIO {
    addrinfo *parse_host_service_pair(const std::string &host, const std::string &service, bool passive) {
        addrinfo addr_hints = {
            .ai_flags = (passive ? AI_PASSIVE : 0) | AI_NUMERICHOST | AI_NUMERICSERV,
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = 0,
            .ai_addrlen = 0,
            .ai_addr = nullptr,
            .ai_canonname = nullptr,
            .ai_next = nullptr
        };
        addrinfo *addr_info = nullptr;
        int res = getaddrinfo(host.c_str(), service.c_str(), &addr_hints, &addr_info);
        if (res != 0 || !addr_info) {
            throw SystemError(
                "host/service resolution for `"
                + host + ":" + service
                + "` failed: "
                + gai_strerror(res)
            );
        }
        return addr_info;
    }

    FD::sys_t make_server_socket(const std::string &host, const std::string &service) {
        addrinfo *addr_info = parse_host_service_pair(host, service, true);

        int fd = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(addr_info);
            throw SystemError(std::string("socket: ") + strerror(errno));
        }

        int yes = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
            freeaddrinfo(addr_info);
            close(fd);
            throw SystemError(std::string("setsockopt: ") + strerror(errno));
        }

        if (::bind(fd, addr_info->ai_addr, addr_info->ai_addrlen) < 0) {
            freeaddrinfo(addr_info);
            close(fd);
            throw SystemError(std::string("bind: ") + strerror(errno));
        }

        if (listen(fd, 1024) < 0) {
            freeaddrinfo(addr_info);
            close(fd);
            throw SystemError(std::string("listen: ") + strerror(errno));
        }

        freeaddrinfo(addr_info);
        return fd;
    }

    StreamServerFD::StreamServerFD(BasicEventLoop &loop, const std::string &host, const std::string &service)
    : FD(loop, make_server_socket(host, service)) {
    }

    Future<StreamSocketFD> StreamServerFD::accept() {
        return loop.event({.sys_fd = sys_fd(), .types = IOEvent::IN}).map([this](auto) -> StreamSocketFD {
            sockaddr addr{};
            socklen_t len{};
            int fd = ::accept(sys_fd(), &addr, &len);
            if (fd < 0) {
                throw SystemError(std::string("accept: ") + strerror(errno));
            }
            return {loop, fd};
        });
    }

    StreamFD StreamFD::open(BasicEventLoop &loop, const std::filesystem::path &path, std::ios_base::openmode mode) {
        int flags = 0;
        if (mode & std::ios::in) {
            flags = O_RDONLY;
        }
        if (mode & std::ios::out) {
            flags = O_WRONLY;
        }
        if (mode & std::ios::in && mode & std::ios::out) {
            flags = O_RDWR;
        }
        sys_t sys_fd = ::open(path.c_str(), flags);
        return {loop, sys_fd};
    }

    StreamFD StreamFD::steal_system(BasicEventLoop &loop, FD::sys_t sys_fd) {
        return {loop, sys_fd};
    }

    StreamFD::StreamFD(StreamFD &&other) noexcept : FD(std::move(other)) {
    }

    Future<std::size_t> StreamFD::read(size_t size, char *data) const {
        return loop.event({.sys_fd = fd, .types = IOEvent::IN})
            .map([fd = fd, data, size](auto) -> size_t {
                auto read_size = ::read(fd, data, size);
                if (read_size < 0) {
                    throw SystemError(std::string("read: ") + strerror(errno));
                }
                return read_size;
            });
    }

    Future<std::size_t> StreamFD::write(size_t size, const char *data) const {
        return loop.event({.sys_fd = fd, .types = IOEvent::OUT})
            .map([fd = fd, data, size](auto) -> size_t {
                auto write_size = ::write(fd, data, size);
                if (write_size < 0) {
                    throw SystemError(std::string("write: ") + strerror(errno));
                }
                return write_size;
            });
    }

    StreamFD::StreamFD(BasicEventLoop &loop, sys_t sys_fd) : FD(loop, sys_fd) {
    }

    Future<StreamSocketFD>
    StreamSocketFD::connect(BasicEventLoop &loop, const std::string &host, const std::string &service) {
        addrinfo *addr_info = parse_host_service_pair(host, service, false);

        int fd = socket(addr_info->ai_family, addr_info->ai_socktype | SOCK_NONBLOCK, addr_info->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(addr_info);
            throw SystemError(std::string("socket: ") + strerror(errno));
        }

        if (::connect(fd, addr_info->ai_addr, addr_info->ai_addrlen) < 0 && errno != EINPROGRESS) {
            freeaddrinfo(addr_info);
            close(fd);
            throw SystemError(std::string("connect: ") + strerror(errno));
        }

        freeaddrinfo(addr_info);

        return loop.event({.sys_fd = fd, .types = IOEvent::OUT}).map([&loop, fd] (auto) -> StreamSocketFD {
            int err = 0;
            socklen_t err_len = sizeof err;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0) {
                close(fd);
                throw SystemError(std::string("getsockopt: ") + strerror(errno));
            }
            if (err) {
                close(fd);
                throw SystemError(std::string("connect: ") + strerror(err));
            }

            return {loop, fd};
        });
    }

    StreamSocketFD::StreamSocketFD(StreamSocketFD &&other) noexcept : StreamFD(std::move(other)) {
    }

    void StreamSocketFD::shutdown(bool read, bool write) const {
        int how = 0;
        if (read && write) {
            how = SHUT_RDWR;
        } else if (read) {
            how = SHUT_RD;
        } else if (write) {
            how = SHUT_WR;
        } else {
            assertion_failed("invalid shutdown mode");
        }
        ::shutdown(sys_fd(), how);
    }

    StreamSocketFD::~StreamSocketFD() {
        if (fd != -1) {
            shutdown();
        }
    }

    StreamSocketFD::StreamSocketFD(BasicEventLoop &loop, sys_t sys_fd) : StreamFD(loop, sys_fd) {
    }

    StreamServerFD::StreamServerFD(StreamServerFD &&other) noexcept : FD(std::move(other)) {
    }
}