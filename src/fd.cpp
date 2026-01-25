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
  fcntl(sys_fd, F_SETFL, flags);
  return {io, sys_fd};
}

SystemFD FD::release_to_system() && {
  auto fd_tmp = fd;
  fd = -1;
  return fd_tmp;
}

void FD::close() && {
  AIOXX_ASSUME(fd != -1);

  state->io.forget(&state->handle);
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
  return StreamFD(steal_from_system(io, sys_fd));
}

std::optional<StreamFD::StreamSize> StreamFD::try_read(OctetBuffer buffer) const {
  std::ptrdiff_t res = ::read(sys_fd(), buffer.data(), buffer.size());

  // ReSharper disable once CppIdenticalOperandsInBinaryExpression
  if (res == EWOULDBLOCK || res == EAGAIN)
    return std::nullopt;

  if (res < 0)
    throw Error(std::error_code{static_cast<int>(res), std::system_category()}, "read");

  return res;
}

std::optional<StreamFD::StreamSize> StreamFD::try_write(OctetStream stream) const {
  std::ptrdiff_t res = ::write(sys_fd(), stream.data(), stream.size());

  // ReSharper disable once CppIdenticalOperandsInBinaryExpression
  if (res == EWOULDBLOCK || res == EAGAIN)
    return std::nullopt;

  if (res < 0)
    throw Error(std::error_code{static_cast<int>(res), std::system_category()}, "write");

  return res;
}

StreamFD::StreamSize StreamFD::read(OctetBuffer buffer) const {
  Future<void> guard = ready(IN);

  auto res = try_read(buffer);
  if (res.has_value()) {
    std::move(guard).detach();
    return *res;
  }

  scheduler()->await(std::move(guard));
  return *try_read(buffer);
}

StreamFD::StreamSize StreamFD::write(OctetStream stream) const {
  Future<void> guard = ready(OUT);

  auto res = try_write(stream);
  if (res.has_value()) {
    std::move(guard).detach();
    return *res;
  }

  scheduler()->await(std::move(guard));
  return *try_write(stream);
}

} // namespace AIO
