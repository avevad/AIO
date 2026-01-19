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

  [[nodiscard]] Future<void> ready(Direction direction) const;

  [[nodiscard]] BasicEventLoop &get_event_loop() const;

  [[nodiscard]] SystemFD release_to_system() &&;
  ~FD();

protected:
  [[nodiscard]] SystemFD get_sys_fd() const;

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

  StreamFD(StreamFD &&other) noexcept = default;
  StreamFD &operator=(StreamFD &&other) noexcept = default;

  std::size_t read(size_t size, char *data) const;
  std::size_t write(size_t size, const char *data) const;

protected:
  StreamFD(BasicEventLoop &loop, SystemFD sys_fd);
};

} // namespace AIO

#include "aio_bits/fd.tcc"
