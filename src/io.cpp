#include "AIOxx/io.hpp"

#include "AIOxx/fd.hpp"
#include "AIOxx/util.hpp"

#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>

namespace AIO {

IOTasksQueue::Handle::Handle(Handle &&other) noexcept
    : fd(other.fd), queue(other.queue), callback(std::move(other.callback)) {
  other.fd = -1;
}

void IOTasksQueue::Handle::update(EventTypes event_types) {
  queue.update(*this, event_types);
}

IOTasksQueue::Handle::~Handle() {
  if (fd != -1) {
    queue.erase(std::move(*this));
  }
}

IOTasksQueue::Handle::Handle(SystemFD fd, IOTasksQueue &queue, TaskCallback callback)
    : fd(fd), queue(queue), callback(std::make_unique<TaskCallback>(std::move(callback))) {
}

IOTasksQueue::IOTasksQueue() {
  ep_fd = epoll_create1(EPOLL_CLOEXEC);
  if (ep_fd < 0) {
    panic(strerror(errno));
  }
}

IOTasksQueue::Handle IOTasksQueue::push(SystemFD fd, TaskCallback callback) {
  Handle handle(fd, *this, std::move(callback));
  epoll_event ep_evt{.events = 0, .data = {.ptr = handle.callback.get()}};
  if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd, &ep_evt) == -1) {
    panic(strerror(errno));
  }
  size++;
  return handle;
}

void IOTasksQueue::update(Handle &handle, EventTypes event_types) {
  epoll_event ep_evt{.events = 0, .data = {.ptr = handle.callback.get()}};
  if (event_types & IN) {
    ep_evt.events |= EPOLLIN;
  }
  if (event_types & OUT) {
    ep_evt.events |= EPOLLOUT;
  }
  if (event_types & ERR) {
    ep_evt.events |= EPOLLERR;
  }
  if (event_types & HUP) {
    ep_evt.events |= EPOLLHUP;
  }
  if (epoll_ctl(ep_fd, EPOLL_CTL_MOD, handle.fd, &ep_evt) == -1) {
    panic(strerror(errno));
  }
}

void IOTasksQueue::erase(Handle &&handle) {
  epoll_ctl(ep_fd, EPOLL_CTL_DEL, handle.fd, nullptr);
  handle.fd = -1;
  size--;
}

std::optional<IOTasksQueue::Task>
IOTasksQueue::poll(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline) {
  epoll_event ep_evt{};
  int timeout_num = -1;
  if (deadline.has_value()) {
    auto timeout = deadline.value() - std::chrono::steady_clock::now();
    timeout_num = std::chrono::duration_cast<std::chrono::duration<int, std::milli>>(timeout).count();
  }
  int result = epoll_wait(ep_fd, &ep_evt, 1, timeout_num);
  if (result < 0) {
    panic(strerror(errno));
  }
  if (result) {
    EventTypes types = 0;
    if (ep_evt.events & EPOLLIN) {
      types |= IN;
    }
    if (ep_evt.events & EPOLLOUT) {
      types |= OUT;
    }
    if (ep_evt.events & EPOLLERR) {
      types |= ERR;
    }
    if (ep_evt.events & EPOLLHUP) {
      types |= HUP;
    }
    return [callback = static_cast<TaskCallback *>(ep_evt.data.ptr), types] { (*callback)(types); };
  }
  return std::nullopt;
}

bool IOTasksQueue::is_empty() const {
  return size == 0;
}

IOTasksQueue::~IOTasksQueue() {
  close(ep_fd);
}

} // namespace AIO
