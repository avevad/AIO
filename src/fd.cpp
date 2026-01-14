#include "AIOxx/fd.hpp"

#include <cstring>
#include <fcntl.h>

#include "AIOxx/event_loop.hpp"

namespace AIO {

SystemFD FD::release_to_system() && {
  auto fd_tmp = fd;
  fd = -1;
  return fd_tmp;
}

FD::~FD() {
  if (fd != -1) {
    close(fd);
  }
}

SystemFD FD::get_sys_fd() const {
  return fd;
}

BasicEventLoop &FD::get_event_loop() const {
  return state->loop;
}

FD::FD(FD &&other) noexcept : fd(other.fd), state(std::move(other.state)) {
  other.fd = -1;
}

FD &FD::operator=(FD &&other) noexcept {
  state = std::move(other.state);
  fd = other.fd;

  other.fd = -1;

  return *this;
}

Future<void> FD::event(Direction direction) const {
  auto &promise_slot = direction == IN ? state->in_promise : state->out_promise;
  if (promise_slot.has_value()) {
    assertion_failed("repeated FD::event with same direction");
  }
  Future<void> future;
  Promise<void> promise;
  bind(future, promise);
  promise_slot = std::move(promise);
  state->io_handle.value().update(
    (state->in_promise.has_value() ? IOTasksQueue::IN : 0) | (state->out_promise.has_value() ? IOTasksQueue::OUT : 0));
  return future;
}

FD::FD(BasicEventLoop &loop, SystemFD sys_fd) : fd(sys_fd), state(nullptr) {
  state = std::make_unique<State>(loop, std::nullopt);
  state->io_handle.emplace(loop.register_system_fd(fd, [state = state.get()](auto e) { state->io_callback(e); }));
}

void FD::State::io_callback(IOTasksQueue::EventTypes event_types) {
  if (event_types & IOTasksQueue::IN && in_promise.has_value()) {
    std::move(in_promise.value()).fulfill();
    in_promise.reset();
  }
  if (event_types & IOTasksQueue::OUT && out_promise.has_value()) {
    std::move(out_promise.value()).fulfill();
    out_promise.reset();
  }
  io_handle.value().update(
    (in_promise.has_value() ? IOTasksQueue::IN : 0) | (out_promise.has_value() ? IOTasksQueue::OUT : 0));
}

StreamFD StreamFD::open(BasicEventLoop &loop, const std::filesystem::path &path, std::ios_base::openmode mode) {
  int flags = 0;
  if (mode & std::ios::in) {
    flags = O_RDONLY;
  }
  if (mode & std::ios::out) {
    flags = O_WRONLY;
  }
  if (mode & std::ios::in && mode & std::ios::out) {
    flags = O_RDWR;
  }
  SystemFD sys_fd = ::open(path.c_str(), flags);
  return {loop, sys_fd};
}

StreamFD StreamFD::steal_from_system(BasicEventLoop &loop, SystemFD sys_fd) {
  return {loop, sys_fd};
}

StreamFD::StreamFD(StreamFD &&other) noexcept : FD(std::move(other)) {
}

Future<std::size_t> StreamFD::read(size_t size, char *data) const {
  return event(IN).map([fd = get_sys_fd(), data, size] -> size_t {
    auto read_size = ::read(fd, data, size);
    if (read_size < 0) {
      throw SystemError(std::string("read: ") + strerror(errno));
    }
    return read_size;
  });
}

Future<std::size_t> StreamFD::write(size_t size, const char *data) const {
  return event(OUT).map([fd = get_sys_fd(), data, size] -> size_t {
    auto write_size = ::write(fd, data, size);
    if (write_size < 0) {
      throw SystemError(std::string("write: ") + strerror(errno));
    }
    return write_size;
  });
}

StreamFD::StreamFD(BasicEventLoop &loop, SystemFD sys_fd) : FD(loop, sys_fd) {
}

} // namespace AIO
