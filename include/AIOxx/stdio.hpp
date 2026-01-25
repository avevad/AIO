#include "fd.hpp"

namespace AIO {
struct StdIO {
  StreamFD in, out, err;

  static StdIO steal_from_system(BasicScheduler::IO &io);

  void release_to_system() &&;
};
} // namespace AIO
