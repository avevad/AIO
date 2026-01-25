#pragma once

#include <chrono>
#include <functional>
#include <list>

#include "future.hpp"

namespace AIO {
using SystemFD = int;

class IOQueue {
public:
  using Task = std::move_only_function<void()>;

  class [[nodiscard]] Handle {
  public:
    Handle(SystemFD fd);
    Handle(Handle &&other) noexcept;
    Handle &operator=(Handle &&other) noexcept;

    Handle(const Handle &) = delete;
    Handle& operator=(const Handle &) = delete;

    Future<void> ready_in();
    Future<void> ready_out();

    ~Handle();

  private:
    friend IOQueue;

    SystemFD fd;
    IOQueue *queue = nullptr;

    // TODO: thread-safety.
    std::optional<Promise<void>> in = std::nullopt, out = std::nullopt;
  };

  IOQueue();
  IOQueue(const IOQueue &) = delete;
  IOQueue(IOQueue &&) noexcept = delete;
  IOQueue &operator=(const IOQueue &) = delete;
  IOQueue &operator=(IOQueue &&) noexcept = delete;

  void add(Handle *handle);
  void erase(Handle *handle);
  [[nodiscard]] std::optional<Task> poll(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline);

  [[nodiscard]] bool empty() const;

  ~IOQueue();

private:
  int ep_fd;
  size_t size = 0;
};
} // namespace AIO
