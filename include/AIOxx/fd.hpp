#pragma once

#include "future.hpp"

#include <fcntl.h>
#include <filesystem>
#include <unistd.h>

namespace AIO {

    class SimpleEventLoop;

    class FD {
    public:
        using sys_t = int;

        FD(const FD &) = delete;
        FD &operator=(const FD &) = delete;

        FD(FD &&other) noexcept;
        FD &operator=(FD &&other) noexcept;

        [[nodiscard]] sys_t sys_fd() const;
        sys_t release_to_system() &&;

        ~FD();

    protected:
        FD(SimpleEventLoop *loop, sys_t sys_fd);

        sys_t fd;
        SimpleEventLoop *loop;
    };

    class StreamFD : public FD {
    public:
        static StreamFD open(SimpleEventLoop *loop, const std::filesystem::path &path, std::ios_base::openmode mode);
        static StreamFD steal_system(SimpleEventLoop *loop, FD::sys_t sys_fd);

        Future<std::size_t> read(size_t buf_size, char *buffer) const;

    protected:
        using FD::FD;
    };

} // namespace AIO
