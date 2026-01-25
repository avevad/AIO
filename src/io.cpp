#include "AIOxx/io.hpp"

#include "AIOxx/fd.hpp"
#include "AIOxx/util.hpp"

#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>

namespace AIO {

IOQueue::Handle::Handle(SystemFD fd) : fd(fd) {
}

IOQueue::Handle::Handle(Handle &&other) noexcept
    : fd(other.fd), queue(other.queue), prom_in(std::move(other.prom_in)), prom_out(std::move(other.prom_out)),
      fut_in(std::move(other.fut_in)), fut_out(std::move(other.fut_out)) {
  AIOXX_ASSUME(other.queue == nullptr);

  other.fd = -1;
  other.queue = nullptr;
}

IOQueue::Handle &IOQueue::Handle::operator=(Handle &&other) noexcept {
  AIOXX_ASSUME(queue == nullptr);
  AIOXX_ASSUME(other.queue == nullptr);

  fd = other.fd;
  queue = other.queue;
  prom_in = std::move(other.prom_in);
  prom_out = std::move(other.prom_out);
  fut_in = std::move(other.fut_in);
  fut_out = std::move(other.fut_out);

  other.fd = -1;
  other.queue = nullptr;

  return *this;
}

Future<void> IOQueue::Handle::ready_in() {
  if (!fut_in.has_value()) {
    auto [promise, future] = Contract<void>();
    prom_in = std::move(promise);
    fut_in = std::move(future);
  }
  auto future = std::move(*fut_in);
  fut_in.reset();
  return future;
}

Future<void> IOQueue::Handle::ready_out() {
  if (!fut_out.has_value()) {
    auto [promise, future] = Contract<void>();
    prom_out = std::move(promise);
    fut_out = std::move(future);
  }
  auto future = std::move(*fut_out);
  fut_out.reset();
  return future;
}

IOQueue::Handle::~Handle() {
  AIOXX_ASSUME(queue == nullptr);
  if (fut_in.has_value())
    std::move(*fut_in).detach();
  if (fut_out.has_value())
    std::move(*fut_out).detach();
}

IOQueue::IOQueue() {
  if ((ep_fd = epoll_create1(EPOLL_CLOEXEC)) < 0)
    panic(strerror(errno));
}

void IOQueue::add(Handle *handle) {
  size++;
  handle->queue = this;
  epoll_event ep_evt{.events = static_cast<uint32_t>(EPOLLIN | EPOLLOUT) | EPOLLET, .data = {.ptr = handle}};
  if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, handle->fd, &ep_evt) == -1)
    panic(strerror(errno));
}

void IOQueue::erase(Handle *handle) {
  if (epoll_ctl(ep_fd, EPOLL_CTL_DEL, handle->fd, nullptr) == -1)
    panic(strerror(errno));
  handle->queue = nullptr;
  size--;
}

std::optional<IOQueue::Task> IOQueue::poll(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline) {
  epoll_event ep_evt{};
  int timeout_num = -1;
  if (deadline.has_value()) {
    auto now = std::chrono::steady_clock::now();
    auto timeout = *deadline < now ? std::chrono::steady_clock::duration{0} : *deadline - now;
    timeout_num = std::chrono::duration_cast<std::chrono::duration<int, std::milli>>(timeout).count();
  }
  int result = epoll_wait(ep_fd, &ep_evt, 1, timeout_num);
  if (result < 0)
    panic(strerror(errno));
  if (result) {
    auto handle = static_cast<Handle *>(ep_evt.data.ptr);
    std::optional<Promise<void>> in = std::nullopt, out = std::nullopt;
    if (ep_evt.events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
      if (!handle->fut_in.has_value()) {
        if (!handle->prom_in.has_value()) {
          auto [promise, future] = Contract<void>();
          handle->prom_in = std::move(promise);
          handle->fut_in = std::move(future);
        }
        in = std::move(handle->prom_in);
        handle->prom_in.reset();
      }
    }
    if (ep_evt.events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) {
      if (!handle->fut_out.has_value()) {
        if (!handle->prom_out.has_value()) {
          auto [promise, future] = Contract<void>();
          handle->prom_out = std::move(promise);
          handle->fut_out = std::move(future);
        }
        out = std::move(handle->prom_out);
        handle->prom_out.reset();
      }
    }
    return [in = std::move(in), out = std::move(out)] mutable {
      if (in.has_value())
        std::move(*in).fulfill();
      if (out.has_value())
        std::move(*out).fulfill();
    };
  }
  return std::nullopt;
}

bool IOQueue::empty() const {
  return size == 0;
}

IOQueue::~IOQueue() {
  close(ep_fd);
}

} // namespace AIO
