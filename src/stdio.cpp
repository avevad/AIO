#include "AIOxx/stdio.hpp"

namespace AIO {

StdIO StdIO::steal_from_system(BasicScheduler::IO &io) {
  return {
    .in = StreamFD(FD::steal_from_system(io, 0)),
    .out = StreamFD(FD::steal_from_system(io, 1)),
    .err = StreamFD(FD::steal_from_system(io, 2))
  };
}

void StdIO::release_to_system() && {
  (void) std::move(in).release_to_system();
  (void) std::move(out).release_to_system();
  (void) std::move(err).release_to_system();
}

} // namespace AIO
