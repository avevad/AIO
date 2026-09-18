#pragma once

#include "fd.hpp"

#include <signal.h>
#include <sys/signalfd.h>

namespace AIO {

class SignalFD : public FD {
public:
  using Info = signalfd_siginfo;

  explicit SignalFD(FD &&other) noexcept;

  // Block the selected signals before opening (in all threads for process-directed signals).
  // Signal masks remain owned by the application: opening, closing, and cancelling a
  // readiness wait do not change them. SIGKILL and SIGSTOP cannot be received.
  static SignalFD open(BasicScheduler::IO &io, const sigset_t &mask);

  SignalFD(SignalFD &&other) noexcept = default;
  SignalFD &operator=(SignalFD &&other) noexcept = default;

  // Consume one signal, or return nullopt if none is pending.
  [[nodiscard]] std::optional<Info> try_read() const;

  // Wait cooperatively in the current fiber until one signal can be consumed.
  // As with FD::ready(IN), only one reader may wait on a descriptor at a time.
  [[nodiscard]] Info read() const;
};

} // namespace AIO
