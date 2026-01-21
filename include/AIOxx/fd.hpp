#pragma once

#include "future.hpp"
#include "io.hpp"

#include <filesystem>

namespace AIO {

class BasicScheduler;

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

  [[nodiscard]] Future<void> ready(Direction direction) const;

  [[nodiscard]] BasicScheduler *scheduler() const;

  [[nodiscard]] SystemFD release_to_system() &&;
  ~FD();

protected:
  [[nodiscard]] SystemFD sys_fd() const;

  FD(BasicScheduler *sched, SystemFD sys_fd);

private:
  struct State {
    BasicScheduler *sched;
    std::optional<IOTasksQueue::Handle> io_handle;
    std::optional<Promise<void>> in_promise = std::nullopt;
    std::optional<Promise<void>> out_promise = std::nullopt;

    void io_callback(IOTasksQueue::EventTypes event_types);
  };

  SystemFD fd;
  std::shared_ptr<State> state; // TODO: remove this temporary fix for use-after-free
  // If we use unique_ptr here, we will have to store raw .get() in IO callback,
  // which can already be expired at the moment the callback is executed.
};

class StreamFD : public FD {
public:
  static StreamFD open(BasicScheduler *sched, const std::filesystem::path &path, std::ios_base::openmode mode);
  static StreamFD steal_from_system(BasicScheduler *sched, SystemFD sys_fd);

  StreamFD(StreamFD &&other) noexcept = default;
  StreamFD &operator=(StreamFD &&other) noexcept = default;

  std::size_t read(size_t size, char *data) const;
  std::size_t write(size_t size, const char *data) const;

protected:
  StreamFD(BasicScheduler *sched, SystemFD sys_fd);
};

} // namespace AIO
