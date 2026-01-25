#include "AIOxx/fd.hpp"

#include <cstring>
#include <fcntl.h>

#include "AIOxx/io.hpp"
#include "AIOxx/scheduler.hpp"

namespace AIO {

FD::FD(FD &&other) noexcept : fd(other.fd), state(std::move(other.state)) {
  other.fd = -1;
}

FD &FD::operator=(FD &&other) noexcept {
  AIOXX_ASSUME(fd == -1);

  state = std::move(other.state);
  fd = other.fd;

  other.fd = -1;

  return *this;
}

Future<void> FD::ready(Direction direction) const {
  AIOXX_ASSUME(fd != -1);

  switch (direction) {
  case IN:
    return state->handle.ready_in();
  case OUT:
    return state->handle.ready_out();
  }
}

FD FD::steal_from_system(BasicScheduler::IO &io, SystemFD sys_fd) {
  // Make sys_fd non-blocking.
  int flags = ::fcntl(sys_fd, F_GETFL, 0);
  if (flags == -1)
    panic(strerror(errno));
  fcntl(sys_fd, F_SETFL, flags | O_NONBLOCK);
  return {io, sys_fd};
}

SystemFD FD::release_to_system() && {
  AIOXX_ASSUME(fd != -1);

  auto sys_fd = fd;
  state->io.forget(&state->handle);
  state.reset();

  fd = -1;
  return sys_fd;
}

void FD::close() && {
  AIOXX_ASSUME(fd != -1);

  state->io.forget(&state->handle);
  state.reset();

  ::close(fd);
  fd = -1;
}

FD::~FD() {
  AIOXX_ASSUME(fd == -1);
}

SystemFD FD::sys_fd() const {
  AIOXX_ASSUME(fd != -1);
  return fd;
}

BasicScheduler *FD::scheduler() const {
  return state->io.scheduler();
}

FD::FD(BasicScheduler::IO &io, SystemFD sys_fd) : fd(sys_fd), state(nullptr) {
  state = std::make_unique<State>(io, IOQueue::Handle{fd});
  state->io.watch(&state->handle);
}

StreamFD::StreamFD(FD &&other) noexcept : FD(std::move(other)) {
}

StreamFD StreamFD::open(BasicScheduler::IO &io, const std::filesystem::path &path, std::ios_base::openmode mode) {
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
  if (sys_fd < 0) {
    throw Error(errno, std::generic_category(), "open(`" + path.string() + "`, ...)");
  }
  return StreamFD(steal_from_system(io, sys_fd));
}

std::optional<StreamFD::StreamSize> StreamFD::try_read(OctetBuffer buffer) const {
  std::ptrdiff_t res = ::read(sys_fd(), buffer.data(), buffer.size());

  // ReSharper disable once CppIdenticalOperandsInBinaryExpression
  if (res < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
    return std::nullopt;

  if (res < 0)
    throw Error(errno, std::generic_category(), "read(" + std::to_string(sys_fd()) + ", ...)");

  return res;
}

std::optional<StreamFD::StreamSize> StreamFD::try_write(OctetStream stream) const {
  std::ptrdiff_t res = ::write(sys_fd(), stream.data(), stream.size());

  // ReSharper disable once CppIdenticalOperandsInBinaryExpression
  if (res < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
    return std::nullopt;

  if (res < 0)
    throw Error(errno, std::generic_category(), "write(" + std::to_string(sys_fd()) + ", ...)");

  return res;
}

StreamFD::StreamSize StreamFD::read(OctetBuffer buffer) const {
  auto res = try_read(buffer);
  if (res.has_value())
    return *res;
  scheduler()->await(ready(IN));
  return *try_read(buffer);
}

StreamFD::StreamSize StreamFD::write(OctetStream stream) const {
  auto res = try_write(stream);
  if (res.has_value())
    return *res;
  scheduler()->await(ready(OUT));
  return *try_write(stream);
}

} // namespace AIO
