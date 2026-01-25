#pragma once

#include "future.hpp"
#include "io.hpp"

#include <filesystem>

#include "scheduler.hpp"

namespace AIO {

class FD {
public:
  class Error : public std::system_error {
    using std::system_error::system_error;
  };

  enum Direction { IN, OUT };

  FD(const FD &) = delete;
  FD &operator=(const FD &) = delete;

  FD(FD &&other) noexcept;
  FD &operator=(FD &&other) noexcept;

  [[nodiscard]] Future<void> ready(Direction direction) const;

  static FD steal_from_system(BasicScheduler::IO &io, SystemFD sys_fd);

  [[nodiscard]] SystemFD release_to_system() &&;
  void close() &&;

  ~FD();

protected:
  [[nodiscard]] SystemFD sys_fd() const;
  [[nodiscard]] BasicScheduler *scheduler() const;

private:
  struct State {
    BasicScheduler::IO &io;
    IOQueue::Handle handle;
  };

  FD(BasicScheduler::IO &io, SystemFD sys_fd);

  SystemFD fd;
  std::unique_ptr<State> state;
};

class StreamFD : public FD {
public:
  using Octet = uint8_t;
  using OctetStream = std::span<const Octet>;
  using OctetBuffer = std::span<Octet>;
  using StreamSize = std::ptrdiff_t;

  explicit StreamFD(FD &&other) noexcept;

  static StreamFD open(BasicScheduler::IO &io, const std::filesystem::path &path, std::ios_base::openmode mode);

  StreamFD(StreamFD &&other) noexcept = default;
  StreamFD &operator=(StreamFD &&other) noexcept = default;

  [[nodiscard]] std::optional<StreamSize> try_read(OctetBuffer buffer) const;
  [[nodiscard]] std::optional<StreamSize> try_write(OctetStream stream) const;

  [[nodiscard]] StreamSize read(OctetBuffer buffer) const;
  [[nodiscard]] StreamSize write(OctetStream stream) const;
};

} // namespace AIO
