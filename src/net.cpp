#include "AIOxx/net.hpp"

#include <cstring>
#include <fcntl.h>
#include <netdb.h>

namespace AIO {
addrinfo *parse_host_service_pair(const std::string &host, const std::string &service, bool passive) {
  addrinfo addr_hints = {
    .ai_flags = (passive ? AI_PASSIVE : 0) | AI_NUMERICHOST | AI_NUMERICSERV,
    .ai_family = AF_UNSPEC,
    .ai_socktype = SOCK_STREAM,
    .ai_protocol = 0,
    .ai_addrlen = 0,
    .ai_addr = nullptr,
    .ai_canonname = nullptr,
    .ai_next = nullptr
  };
  addrinfo *addr_info = nullptr;
  int res = getaddrinfo(host.c_str(), service.c_str(), &addr_hints, &addr_info);
  if (res != 0 || !addr_info) {
    throw SystemError("host/service resolution for `" + host + ":" + service + "` failed: " + gai_strerror(res));
  }
  return addr_info;
}

SystemFD make_server_socket(const std::string &host, const std::string &service) {
  addrinfo *addr_info = parse_host_service_pair(host, service, true);

  int fd = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(addr_info);
    throw SystemError(std::string("socket: ") + strerror(errno));
  }

  int yes = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    freeaddrinfo(addr_info);
    close(fd);
    throw SystemError(std::string("setsockopt: ") + strerror(errno));
  }

  if (::bind(fd, addr_info->ai_addr, addr_info->ai_addrlen) < 0) {
    freeaddrinfo(addr_info);
    close(fd);
    throw SystemError(std::string("bind: ") + strerror(errno));
  }

  if (listen(fd, 1024) < 0) {
    freeaddrinfo(addr_info);
    close(fd);
    throw SystemError(std::string("listen: ") + strerror(errno));
  }

  freeaddrinfo(addr_info);
  return fd;
}

StreamServerFD::StreamServerFD(BasicScheduler *sched, const std::string &host, const std::string &service)
    : FD(sched, make_server_socket(host, service)) {
}

Future<StreamSocketFD> StreamServerFD::accept() {
  return ready(IN).map_result([this] -> StreamSocketFD {
    sockaddr addr{};
    socklen_t len{};
    int fd = ::accept(sys_fd(), &addr, &len);
    if (fd < 0) {
      throw SystemError(std::string("accept: ") + strerror(errno));
    }
    return {scheduler(), fd};
  });
}

Future<StreamSocketFD>
StreamSocketFD::connect(BasicScheduler *sched, const std::string &host, const std::string &service) {
  addrinfo *addr_info = parse_host_service_pair(host, service, false);

  int fd = socket(addr_info->ai_family, addr_info->ai_socktype | SOCK_NONBLOCK, addr_info->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(addr_info);
    throw SystemError(std::string("socket: ") + strerror(errno));
  }

  if (::connect(fd, addr_info->ai_addr, addr_info->ai_addrlen) < 0 && errno != EINPROGRESS) {
    freeaddrinfo(addr_info);
    close(fd);
    throw SystemError(std::string("connect: ") + strerror(errno));
  }

  freeaddrinfo(addr_info);

  StreamSocketFD socket_fd(sched, fd);
  auto connected = socket_fd.ready(OUT);
  return std::move(connected).map_result([socket_fd = std::move(socket_fd)] mutable -> StreamSocketFD {
    int err = 0;
    socklen_t err_len = sizeof err;
    if (getsockopt(socket_fd.sys_fd(), SOL_SOCKET, SO_ERROR, &err, &err_len) != 0) {
      close(socket_fd.sys_fd());
      throw SystemError(std::string("getsockopt: ") + strerror(errno));
    }
    if (err) {
      close(socket_fd.sys_fd());
      throw SystemError(std::string("connect: ") + strerror(err));
    }

    return std::move(socket_fd);
  });
}

void StreamSocketFD::shutdown(bool read, bool write) const {
  int how = 0;
  if (read && write) {
    how = SHUT_RDWR;
  } else if (read) {
    how = SHUT_RD;
  } else if (write) {
    how = SHUT_WR;
  } else {
    AIOXX_UNREACHABLE;
  }
  ::shutdown(sys_fd(), how);
}

StreamSocketFD::~StreamSocketFD() {
  if (sys_fd() != -1) {
    shutdown();
  }
}

StreamSocketFD::StreamSocketFD(BasicScheduler *sched, SystemFD sys_fd) : StreamFD(sched, sys_fd) {
}

StreamServerFD::StreamServerFD(StreamServerFD &&other) noexcept : FD(std::move(other)) {
}
} // namespace AIO
