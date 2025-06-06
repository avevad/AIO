#include "fd.hpp"

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

} // namespace AIO
