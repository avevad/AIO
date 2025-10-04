#include "AIOxx/fd.hpp"

#include "AIOxx/event_loop.hpp"

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
    }

    FD &FD::operator=(FD &&other) noexcept {
        if (&other.loop != &loop) {
            assertion_failed("assignment of FDs between different event loops");
        }
        fd = other.fd;

        other.fd = -1;

        return *this;
    }

    FD::FD(BasicEventLoop &loop, sys_t sys_fd) : fd(sys_fd), loop(loop) {
    }

} // namespace AIO
