#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <filesystem>

namespace AIO {

    class SimpleEventLoop;

    class FD {
    public:
        using sys_t = int;

        [[nodiscard]] sys_t sys_fd() const;

        ~FD();

        FD(const FD &) = delete;
        FD &operator=(const FD &) = delete;

        FD(FD &&other) noexcept;

        FD &operator=(FD &&other) noexcept;

    protected:
        FD(SimpleEventLoop *loop, sys_t sys_fd);

        sys_t fd;
        SimpleEventLoop *loop;
    };

    class StreamFD : public FD {
    public:
        static StreamFD open(SimpleEventLoop *loop, const std::filesystem::path &path, std::ios_base::openmode mode);
        static StreamFD steal_system(SimpleEventLoop *loop, FD::sys_t sys_fd);

    protected:
        using FD::FD;
    };

} // namespace AIO
