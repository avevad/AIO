#include "fd.hpp"

#include <cstring>

#include "event_loop.hpp"
#include "future.hpp"

namespace AIO {

    FD::sys_t FD::sys_fd() const {
        return fd;
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

    FD::FD(SimpleEventLoop *loop, sys_t sys_fd) : fd(sys_fd), loop(loop) {
    }

    StreamFD StreamFD::open(SimpleEventLoop *loop, const std::filesystem::path &path, std::ios_base::openmode mode) {
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

    StreamFD StreamFD::steal_system(SimpleEventLoop *loop, FD::sys_t sys_fd) {
        return {loop, sys_fd};
    }

    Future<std::size_t> StreamFD::read(size_t buf_size, char *buffer) const {
        return loop->event({.sys_fd = fd, .types = IOEvent::IN}).then(loop->async([
            fd = fd, buffer, buf_size
        ] (auto) -> size_t {
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


} // namespace AIO
