#pragma once

#include <chrono>
#include <cstring>
#include <sys/epoll.h>

namespace AIO {

    struct IOEvent {
        enum Type : uint8_t {
            IN, OUT, ERR, HUP
        };
        using Types = uint8_t;
        using Callback = std::move_only_function<void(Types)>;

        using sys_fd_t = int;

        sys_fd_t sys_fd;
        Types types;
    };

    class IOQueue {
    public:
        IOQueue() {
            epfd = epoll_create1(EPOLL_CLOEXEC);
            if (epfd < 0) {
                assertion_failed(strerror(errno));
            }
        }

        void register_event(IOEvent event, IOEvent::Callback *callback, bool oneshot) {
            epoll_event epe {.events = 0, .data = {.ptr = callback}};
            if (event.types & IOEvent::IN) {
                epe.events |= EPOLLIN;
            }
            if (event.types & IOEvent::OUT) {
                epe.events |= EPOLLOUT;
            }
            if (oneshot) {
                epe.events |= EPOLLONESHOT;
            }
            if (epoll_ctl(epfd, EPOLL_CTL_ADD, event.sys_fd, &epe) == -1) {
                assertion_failed(strerror(errno));
            }
        }

        void deregister_event(IOEvent::sys_fd_t fd) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        }

        void poll_event(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline) {
            epoll_event epe {};
            int timeout_num = -1;
            if (deadline.has_value()) {
                auto timeout = deadline.value() - std::chrono::steady_clock::now();
                timeout_num = std::chrono::duration_cast<std::chrono::duration<int, std::milli>>(timeout).count();
            }
            int result = epoll_wait(epfd, &epe, 1, timeout_num);
            if (result < 0) {
                assertion_failed(strerror(errno));
            }
            if (result) {
                uint8_t types = 0;
                if (epe.events & EPOLLIN) {
                    types |= IOEvent::IN;
                }
                if (epe.events & EPOLLOUT) {
                    types |= IOEvent::OUT;
                }
                if (epe.events & EPOLLERR) {
                    types |= IOEvent::ERR;
                }
                if (epe.events & EPOLLHUP) {
                    types |= IOEvent::HUP;
                }
                auto &callback = *static_cast<IOEvent::Callback *>(epe.data.ptr);
                callback(types);
            }
        }

        ~IOQueue() {
            close(epfd);
        }

    private:
        int epfd;
    };

}
