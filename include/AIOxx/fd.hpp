#pragma once

#include "future.hpp"
#include "io.hpp"

#include <filesystem>

namespace AIO {

    class BasicEventLoop;

    class SystemError : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    class FD {
    public:
        enum Direction { IN, OUT };

        FD(const FD &) = delete;
        FD &operator=(const FD &) = delete;

        FD(FD &&other) noexcept;
        FD &operator=(FD &&other) noexcept;

        [[nodiscard]] Future<void> event(Direction direction) const;

        [[nodiscard]] SystemFD release_to_system() &&;
        ~FD();

    protected:
        [[nodiscard]] SystemFD get_sys_fd() const;
        [[nodiscard]] BasicEventLoop &get_event_loop() const;

        FD(BasicEventLoop &loop, SystemFD sys_fd);

    private:
        struct State {
            BasicEventLoop &loop;
            std::optional<IOTasksQueue::Handle> io_handle;
            std::optional<Promise<void>> in_promise = std::nullopt;
            std::optional<Promise<void>> out_promise = std::nullopt;

            void io_callback(IOTasksQueue::EventTypes event_types);
        };

        SystemFD fd;
        std::unique_ptr<State> state;
    };

    class StreamFD : public FD {
    public:
        static StreamFD open(BasicEventLoop &loop, const std::filesystem::path &path, std::ios_base::openmode mode);
        static StreamFD steal_from_system(BasicEventLoop &loop, SystemFD sys_fd);

        StreamFD(StreamFD &&other) noexcept;
        using FD::operator=;

        Future<std::size_t> read(size_t size, char *data) const;
        Future<std::size_t> write(size_t size, const char *data) const;

    protected:
        StreamFD(BasicEventLoop &loop, SystemFD sys_fd);
    };

    template<std::derived_from<StreamFD> BaseFD>
    class BufferedStreamFD : public BaseFD {
    public:
        constexpr static size_t ICAP_DEFAULT = 1024, OCAP_DEFAULT = 1024;

        explicit BufferedStreamFD(BaseFD base);

        Future<std::size_t> read(size_t size, char *data) const;
        Future<std::optional<char>> read_byte() const;
        Future<std::string> read_until(char delim, size_t limit = std::numeric_limits<size_t>::max());

        Future<std::size_t> write(size_t size, const char *data) const;
        Future<void> write_string(std::string_view str) const;

        [[nodiscard]] Future<bool> flush() const;

    private:
        Future<std::size_t> read_some(size_t size, char *data) const;
        Future<std::size_t> write_some(size_t size, const char *data) const;

        std::unique_ptr<char[]> i_buf = std::make_unique<char[]>(ICAP_DEFAULT);
        std::unique_ptr<char[]> o_buf = std::make_unique<char[]>(OCAP_DEFAULT);
        mutable size_t i_beg = 0, i_sz = 0, o_sz = 0;
        size_t i_cap = ICAP_DEFAULT, o_cap = OCAP_DEFAULT;
    };

} // namespace AIO

#include "aio_bits/fd.tcc"
