#pragma once

#include <chrono>
#include <functional>
#include <list>

namespace AIO {
using SystemFD = int;

class IOTasksQueue {
public:
  enum EventType : uint8_t { IN = 1, OUT = 2, ERR = 4, HUP = 8 };
  using EventTypes = uint8_t;
  using Task = std::move_only_function<void()>;
  using TaskCallback = std::move_only_function<void(EventTypes)>;
  class Handle {
  public:
    Handle(Handle &&other) noexcept;

    void update(EventTypes event_types);

    ~Handle();

  private:
    friend IOTasksQueue;

    Handle(SystemFD fd, IOTasksQueue &queue, TaskCallback callback);

    SystemFD fd;
    IOTasksQueue &queue;
    std::unique_ptr<TaskCallback> callback;
  };

  IOTasksQueue();

  Handle push(SystemFD fd, TaskCallback callback);
  void update(Handle &handle, EventTypes event_types);
  void erase(Handle &&handle);
  std::optional<Task> poll(std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline);

  [[nodiscard]] bool is_empty() const;

  ~IOTasksQueue();

private:
  int ep_fd;
  size_t size = 0;
};

} // namespace AIO
