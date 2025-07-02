#include "AIOxx/fd.hpp"

#include "AIOxx/event_loop.hpp"
#include "AIOxx/future.hpp"

#include <cstring>
#include <netdb.h>
#include <sys/fcntl.h>

namespace AIO {

    FD::sys_t FD::sys_fd() const {
        return fd;
    }

    FD::sys_t FD::release_to_system() && {
        auto fd_tmp = fd;
        fd = -1;
        return fd_tmp;
    }

    FD::~FD() {
        if (fd != -1) {
            close(fd);
        }
    }

    FD::FD(FD &&other) noexcept : fd(other.fd), loop(other.loop) {
        other.fd = -1;
        other.loop = nullptr;
    }

    FD &FD::operator=(FD &&other) noexcept {
        fd = other.fd;
        loop = other.loop;

        other.fd = -1;
        other.loop = nullptr;

        return *this;
    }

    FD::FD(BasicEventLoop *loop, sys_t sys_fd) : fd(sys_fd), loop(loop) {
    }

    FD::sys_t make_server_socket(const std::string &hostname, const std::string &service) {
        addrinfo addr_hints = {
            .ai_flags = AI_PASSIVE,
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = 0,
            .ai_addrlen = 0,
            .ai_addr = nullptr,
            .ai_canonname = nullptr,
            .ai_next = nullptr};
        addrinfo *addr_info = nullptr;

        int res = getaddrinfo(hostname.c_str(), service.c_str(), &addr_hints, &addr_info);
        if (res != 0) {
            throw SystemError("name resolution for `" + hostname + ":" + service + "` failed: " + gai_strerror(res));
        }

        std::string last_error = "no address resolved";
        int fd = 0;
        addrinfo *conn_addr = addr_info;
        for (; conn_addr != nullptr; conn_addr = conn_addr->ai_next) {
            fd = socket(conn_addr->ai_family, conn_addr->ai_socktype, conn_addr->ai_protocol);
            if (fd < 0) {
                last_error = std::string("socket: ") + strerror(errno);
                continue;
            }

            if (::bind(fd, conn_addr->ai_addr, conn_addr->ai_addrlen) < 0) {
                last_error = std::string("bind: ") + strerror(errno);
                close(fd);
                continue;
            }

            break;
        }

        freeaddrinfo(addr_info);

        if (conn_addr == nullptr) {
            throw SystemError("`" + hostname + ":" + service + "`: " + last_error);
        }

        return fd;
    }

    StreamServerFD::StreamServerFD(BasicEventLoop *loop, const std::string &hostname, const std::string &service)
    : FD(loop, make_server_socket(hostname, service)) {
        if (listen(fd, 1024) < 0) {
            close(fd);
            throw SystemError("`" + hostname + ":" + service + "`: listen: " + strerror(errno));
        }
    }

    Future<StreamSocketFD> StreamServerFD::accept() {
        return loop->async_execute([this]() -> StreamSocketFD {
            loop->await(loop->event({.sys_fd = sys_fd(), .types = IOEvent::IN}));
            sockaddr addr{};
            socklen_t len{};
            int fd = ::accept(sys_fd(), &addr, &len);
            if (fd < 0) {
                throw SystemError(std::string("accept: ") + strerror(errno));
            }
            return {loop, fd};
        });
    }

    StreamFD StreamFD::open(BasicEventLoop *loop, const std::filesystem::path &path, std::ios_base::openmode mode) {
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

    StreamFD StreamFD::steal_system(BasicEventLoop *loop, FD::sys_t sys_fd) {
        return {loop, sys_fd};
    }

    StreamFD::StreamFD(StreamFD &&other) noexcept : FD(std::move(other)) {
    }

    Future<std::size_t> StreamFD::read(size_t buf_size, char *buffer) const {
        return loop->event({.sys_fd = fd, .types = IOEvent::IN})
            .then(loop->async([fd = fd, buffer, buf_size](auto) -> size_t {
                if (buf_size == 0) {
                    return 0;
                }
                // this wouldn't block because some event has definitely happened (either exceptional or not)
                auto read_size = ::read(fd, buffer, buf_size);
                if (read_size < 0) {
                    assertion_failed(strerror(errno));
                }
                return read_size;
            }));
    }

    StreamFD::StreamFD(BasicEventLoop *loop, sys_t sys_fd) : FD(loop, sys_fd) {
    }

    Future<StreamSocketFD>
    StreamSocketFD::connect(BasicEventLoop *loop, const std::string &hostname, const std::string &service) {
        return loop->async_execute([loop, hostname, service] () -> StreamSocketFD {
            addrinfo addr_hints = {
                .ai_flags = 0,
                .ai_family = AF_UNSPEC,
                .ai_socktype = SOCK_STREAM,
                .ai_protocol = 0,
                .ai_addrlen = 0,
                .ai_addr = nullptr,
                .ai_canonname = nullptr,
                .ai_next = nullptr};
            addrinfo *addr_info = nullptr;

            int res = getaddrinfo(hostname.c_str(), service.c_str(), &addr_hints, &addr_info);
            if (res != 0) {
                throw SystemError("name resolution for `" + hostname + ":" + service + "` failed: " + gai_strerror(res));
            }

            std::string last_error = "no address resolved";
            int fd = 0;
            addrinfo *conn_addr = addr_info;
            for (; conn_addr != nullptr; conn_addr = conn_addr->ai_next) {
                fd = socket(conn_addr->ai_family, conn_addr->ai_socktype | SOCK_NONBLOCK, conn_addr->ai_protocol);
                if (fd < 0) {
                    last_error = std::string("socket: ") + strerror(errno);
                    continue;
                }

                int conn_res = ::connect(fd, conn_addr->ai_addr, conn_addr->ai_addrlen);
                if (conn_res < 0) {
                    if (errno == EINPROGRESS) {
                        loop->await(loop->event({.sys_fd = fd, .types = IOEvent::OUT}));
                        int err = 0;
                        socklen_t err_len = sizeof err;
                        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0) {
                            err = errno;
                        }
                        if (err) {
                            last_error = std::string("connect/getsockopt: ") + strerror(err);
                            close(fd);
                            continue;
                        }
                    } else {
                        last_error = std::string("connect: ") + strerror(errno);
                        close(fd);
                        continue;
                    }
                }

                break;
            }

            freeaddrinfo(addr_info);

            if (conn_addr == nullptr) {
                throw SystemError("`" + hostname + ":" + service + "`: " + last_error);
            }

            return {loop, fd};
        });
    }

    StreamSocketFD::StreamSocketFD(StreamSocketFD &&other) noexcept : StreamFD(std::move(other)) {
    }

    void StreamSocketFD::shutdown(bool read, bool write) {
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

    StreamSocketFD::StreamSocketFD(BasicEventLoop *loop, sys_t sys_fd) : StreamFD(loop, sys_fd) {
    }

    StreamServerFD::StreamServerFD(StreamServerFD &&other) noexcept : FD(std::move(other)) {
    }

} // namespace AIO
