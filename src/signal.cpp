#include "AIOxx/signal.hpp"

#include <cerrno>
#include <unistd.h>

namespace AIO {

SignalFD::SignalFD(FD &&other) noexcept : FD(std::move(other)) {
}

SignalFD SignalFD::open(BasicScheduler::IO &io, const sigset_t &mask) {
  if (sigismember(&mask, SIGKILL) || sigismember(&mask, SIGSTOP))
    throw Error(EINVAL, std::generic_category(), "signalfd: SIGKILL and SIGSTOP cannot be received");

  auto fd = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (fd < 0)
    throw Error(errno, std::generic_category(), "signalfd");

  try {
    return SignalFD(FD::steal_from_system(io, fd));
  } catch (...) {
    ::close(fd);
    throw;
  }
}

std::optional<SignalFD::Info> SignalFD::try_read() const {
  Info info{};
  ssize_t count;
  do {
    count = ::read(sys_fd(), &info, sizeof(info));
  } while (count < 0 && errno == EINTR);

  if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return std::nullopt;
  if (count < 0)
    throw Error(errno, std::generic_category(), "read(signalfd)");
  if (count != static_cast<ssize_t>(sizeof(info)))
    throw Error(EIO, std::generic_category(), "read(signalfd): incomplete signal record");
  return info;
}

SignalFD::Info SignalFD::read() const {
  while (true) {
    if (auto info = try_read())
      return *info;
    scheduler()->await(ready(IN));
  }
}

} // namespace AIO
