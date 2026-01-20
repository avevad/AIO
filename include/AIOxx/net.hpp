#pragma once

#include "fd.hpp"

namespace AIO {
class StreamSocketFD : public StreamFD {
public:
  static Future<StreamSocketFD> connect(BasicScheduler *sched, const std::string &host, const std::string &service);

  StreamSocketFD(StreamSocketFD &&other) noexcept = default;
  StreamSocketFD &operator=(StreamSocketFD &&other) noexcept = default;

  void shutdown(bool read = true, bool write = true) const;

  ~StreamSocketFD();

private:
  StreamSocketFD(BasicScheduler *sched, SystemFD sys_fd);

  friend class StreamServerFD;
};

class StreamServerFD : public FD {
public:
  StreamServerFD(StreamServerFD &&other) noexcept;
  StreamServerFD &operator=(StreamServerFD &&other) noexcept = default;

  StreamServerFD(BasicScheduler *sched, const std::string &host, const std::string &service);

  Future<StreamSocketFD> accept();
};

} // namespace AIO
