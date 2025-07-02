#pragma once

#include "future.hpp"

#include <filesystem>

namespace AIO {

    class BasicEventLoop;

    class SystemError : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

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
        FD(BasicEventLoop *loop, sys_t sys_fd);

        sys_t fd;
        BasicEventLoop *loop;
    };

    class StreamFD : public FD {
    public:
        static StreamFD open(BasicEventLoop *loop, const std::filesystem::path &path, std::ios_base::openmode mode);
        static StreamFD steal_system(BasicEventLoop *loop, FD::sys_t sys_fd);

        StreamFD(StreamFD &&other) noexcept;
        using FD::operator=;

        Future<std::size_t> read(size_t buf_size, char *buffer) const;

    protected:
        StreamFD(BasicEventLoop *loop, sys_t sys_fd);
    };

    class StreamSocketFD : public StreamFD {
    public:
        static Future<StreamSocketFD> connect(BasicEventLoop *loop, const std::string &hostname, const std::string &service);

        StreamSocketFD(StreamSocketFD &&other) noexcept;
        using StreamFD::operator=;

        void shutdown(bool read = true, bool write = true);

        ~StreamSocketFD();

    private:
        StreamSocketFD(BasicEventLoop *loop, sys_t sys_fd);

        friend class StreamServerFD;
    };

    class StreamServerFD : public FD {
    public:
        StreamServerFD(StreamServerFD &&other) noexcept;
        using FD::operator=;

        StreamServerFD(BasicEventLoop *loop, const std::string &hostname, const std::string &service);

        Future<StreamSocketFD> accept();
    };

} // namespace AIO
