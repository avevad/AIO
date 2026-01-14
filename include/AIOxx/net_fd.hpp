#pragma once

#include "fd.hpp"

namespace AIO {
class StreamSocketFD : public StreamFD {
public:
  static Future<StreamSocketFD> connect(BasicEventLoop &loop, const std::string &host, const std::string &service);

  StreamSocketFD(StreamSocketFD &&other) noexcept;
  using StreamFD::operator=;

  void shutdown(bool read = true, bool write = true) const;

  ~StreamSocketFD();

private:
  StreamSocketFD(BasicEventLoop &loop, SystemFD sys_fd);

  friend class StreamServerFD;
};

class StreamServerFD : public FD {
public:
  StreamServerFD(StreamServerFD &&other) noexcept;
  using FD::operator=;

  StreamServerFD(BasicEventLoop &loop, const std::string &host, const std::string &service);

  Future<StreamSocketFD> accept();
};

} // namespace AIO
