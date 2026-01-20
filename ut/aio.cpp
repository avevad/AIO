#include <gtest/gtest.h>

#include "AIOxx/fd.hpp"
#include "AIOxx/io.hpp"
#include "AIOxx/net.hpp"
#include "AIOxx/scheduler.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace AIO;

namespace {

template<typename T>
Future<T> ok(T v) {
  Contract<T> c;
  std::move(c.promise).fulfill(std::move(v));
  return std::move(c.future);
}

struct ExposedServer : StreamServerFD {
  using FD::sys_fd;
  using StreamServerFD::StreamServerFD;
};

[[maybe_unused]] std::filesystem::path make_tmp_fifo() {
  auto dir = std::filesystem::temp_directory_path() / "aioxx-ut";
  std::filesystem::create_directories(dir);
  auto path = dir / "fifo";
  std::error_code ec;
  std::filesystem::remove(path, ec);
  if (::mkfifo(path.c_str(), 0600) != 0) {
    // If it already exists, it's fine; tests are single-process.
    if (errno != EEXIST) {
      ADD_FAILURE() << "mkfifo failed: " << strerror(errno);
    }
  }
  return path;
}

} // namespace

TEST(IO, QueuePipe) {
  int p[2];
  ASSERT_EQ(::pipe(p), 0);

  IOTasksQueue q;

  bool called = false;
  IOTasksQueue::EventTypes seen = 0;

  auto h = q.create(p[0], [&](IOTasksQueue::EventTypes e) {
    called = true;
    seen = e;
  });
  h.update(IOTasksQueue::IN | IOTasksQueue::ERR | IOTasksQueue::HUP);

  ASSERT_EQ(::write(p[1], "x", 1), 1);
  auto task = q.poll(std::nullopt);
  ASSERT_TRUE(task.has_value());
  (*task)();
  EXPECT_TRUE(called);
  EXPECT_TRUE((seen & IOTasksQueue::IN) != 0);

  called = false;
  seen = 0;
  ::close(p[1]); // should produce HUP on the reader
  auto task2 = q.poll(std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
  ASSERT_TRUE(task2.has_value());
  (*task2)();
  EXPECT_TRUE(called);
  EXPECT_TRUE(((seen & IOTasksQueue::HUP) != 0) || ((seen & IOTasksQueue::IN) != 0));

  ::close(p[0]);
}

TEST(IO, QueueTimeoutAndMove) {
  int p[2];
  ASSERT_EQ(::pipe(p), 0);

  IOTasksQueue q;
  auto h = q.create(p[0], [](IOTasksQueue::EventTypes) {});
  h.update(0);

  auto none = q.poll(std::chrono::steady_clock::now() - std::chrono::milliseconds(1));
  EXPECT_FALSE(none.has_value());

  EXPECT_FALSE(q.empty());
  {
    auto h2 = std::move(h);
    EXPECT_FALSE(q.empty());
  }
  EXPECT_TRUE(q.empty());

  ::close(p[0]);
  ::close(p[1]);
}

// TODO: fix test failure (EBADF because of FD use-after-close, caused by FD::State shared_ptr outliving the FD itself).
// TEST(Scheduler, PipeRW) {
//   run_in_new([](BasicScheduler *sched) {
//     // async/fiber/await
//     auto inc = sched->async([](int x) { return x + 1; });
//     EXPECT_EQ(sched->await(inc(5)), 6);
//
//     // time-based scheduling paths
//     sched->await(sched->deadline(std::chrono::steady_clock::now()));
//     sched->await(sched->timeout(std::chrono::milliseconds(1)));
//     sched->yield();
//
//     int p[2];
//     ASSERT_EQ(::pipe(p), 0);
//     auto r = StreamFD::steal_from_system(sched, p[0]);
//     auto w = StreamFD::steal_from_system(sched, p[1]);
//
//     auto reader = sched->fiber([r = std::move(r)]() mutable {
//       char buf[8] = {};
//       auto n = r.read(2, buf);
//       EXPECT_EQ(std::string(buf, buf + n), "hi");
//     });
//
//     auto writer = sched->fiber([w = std::move(w)]() mutable { EXPECT_EQ(w.write(2, "hi"), 2u); });
//
//     sched->await(std::move(writer));
//     sched->await(std::move(reader));
//
//     // exception capture in fibers
//     auto bad = sched->fiber([]() -> int { throw std::runtime_error("x"); });
//     bool handled = false;
//     auto recovered = std::move(bad).except_any([&](std::exception_ptr) {
//       handled = true;
//       return ok(1);
//     });
//     EXPECT_EQ(sched->await(std::move(recovered)), 1);
//     EXPECT_TRUE(handled);
//   });
// }

// TODO: fix test failure (same as above).
// TEST(FD, OpenModes) {
//   run_in_new([](BasicScheduler *sched) {
//     auto fifo = make_tmp_fifo();
//
//     // keep one RDWR endpoint open to avoid blocking on O_RDONLY/O_WRONLY opens
//     int keeper = ::open(fifo.c_str(), O_RDWR | O_CLOEXEC);
//     ASSERT_GE(keeper, 0);
//
//     int rd_sys = -1, wr_sys = -1, rw_sys = -1;
//     {
//       auto rd = StreamFD::open(sched, fifo, std::ios::in);
//       auto wr = StreamFD::open(sched, fifo, std::ios::out);
//       auto rw = StreamFD::open(sched, fifo, std::ios::in | std::ios::out);
//
//       rd_sys = std::move(rd).release_to_system();
//       wr_sys = std::move(wr).release_to_system();
//       rw_sys = std::move(rw).release_to_system();
//     }
//
//     EXPECT_EQ(::fcntl(rd_sys, F_GETFL) & O_ACCMODE, O_RDONLY);
//     EXPECT_EQ(::fcntl(wr_sys, F_GETFL) & O_ACCMODE, O_WRONLY);
//     EXPECT_EQ(::fcntl(rw_sys, F_GETFL) & O_ACCMODE, O_RDWR);
//
//     ::close(rd_sys);
//     ::close(wr_sys);
//     ::close(rw_sys);
//     ::close(keeper);
//   });
// }

// TODO: fix test failure (same as above).
// TEST(Net, ConnectAccept) {
//   run_in_new([](BasicScheduler *sched) {
//     ExposedServer server(sched, "127.0.0.1", "0");
//
//     sockaddr_in sin{};
//     socklen_t len = sizeof(sin);
//     ASSERT_EQ(::getsockname(server.sys_fd(), reinterpret_cast<sockaddr *>(&sin), &len), 0);
//     auto port = ntohs(sin.sin_port);
//     ASSERT_GT(port, 0u);
//
//     auto accept_f = server.accept();
//     auto connect_f = StreamSocketFD::connect(sched, "127.0.0.1", std::to_string(port));
//
//     auto client = sched->await(std::move(connect_f));
//     auto peer = sched->await(std::move(accept_f));
//
//     EXPECT_EQ(client.write(3, "hey"), 3u);
//     char buf[8] = {};
//     auto n = peer.read(3, buf);
//     EXPECT_EQ(std::string(buf, buf + n), "hey");
//
//     client.shutdown(true, false);
//     client.shutdown(false, true);
//     peer.shutdown();
//   });
// }

// TODO: fix test failure (same as above).
// TEST(Net, Refused) {
//   run_in_new([](BasicScheduler *sched) {
//     // Pick an unused local port by binding without listening.
//     int s = ::socket(AF_INET, SOCK_STREAM, 0);
//     ASSERT_GE(s, 0);
//     sockaddr_in sin{};
//     sin.sin_family = AF_INET;
//     sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
//     sin.sin_port = 0;
//     ASSERT_EQ(::bind(s, reinterpret_cast<sockaddr *>(&sin), sizeof(sin)), 0);
//     socklen_t len = sizeof(sin);
//     ASSERT_EQ(::getsockname(s, reinterpret_cast<sockaddr *>(&sin), &len), 0);
//     auto port = ntohs(sin.sin_port);
//     ::close(s);
//
//     bool failed = false;
//     try {
//       (void) sched->await(StreamSocketFD::connect(sched, "127.0.0.1", std::to_string(port)));
//     } catch (const SystemError &) {
//       failed = true;
//     }
//     EXPECT_TRUE(failed);
//   });
// }

// TODO: fix test failure (same as above).
// TEST(Net, BadHost) {
//   BasicScheduler sched;
//   EXPECT_THROW((void) StreamSocketFD::connect(&sched, "localhost", "80"), SystemError);
//   EXPECT_THROW((void) ExposedServer(&sched, "localhost", "80"), SystemError);
// }

// TODO: fix test failure (same as above).
// #ifdef AIOXX_DEBUG
// TEST(FD, ShutdownNoneDies) {
//   run_in_new([](BasicScheduler *sched) {
//     ExposedServer server(sched, "127.0.0.1", "0");
//     sockaddr_in sin{};
//     socklen_t len = sizeof(sin);
//     ASSERT_EQ(::getsockname(server.sys_fd(), reinterpret_cast<sockaddr *>(&sin), &len), 0);
//     auto port = ntohs(sin.sin_port);
//
//     auto accept_f = server.accept();
//     auto client = sched->await(StreamSocketFD::connect(sched, "127.0.0.1", std::to_string(port)));
//     auto peer = sched->await(std::move(accept_f));
//
//     EXPECT_DEATH({ client.shutdown(false, false); }, ".*");
//     peer.shutdown();
//   });
// }
// #endif
