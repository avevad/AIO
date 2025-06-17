#pragma once

#include "fd.hpp"

#include <chrono>
#include <functional>

namespace AIO {

    struct IOEvent {
        enum Type : uint8_t {
            IN = 1, OUT = 2, ERR = 4, HUP = 8
        };

        using Types = uint8_t;
        using Callback = std::move_only_function<void(Types)>;

        FD::sys_t sys_fd;
        Types types;
    };

    class IOQueue {
    public:
        IOQueue();

        void register_event(IOEvent event, IOEvent::Callback *callback, bool oneshot);
        void deregister_event(FD::sys_t fd);
        void poll_event(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline);

        ~IOQueue();

    private:
        int epfd;
    };

} // namespace AIO