#include "AIOxx/fd.hpp"
#include "AIOxx/scheduler.hpp"

constexpr size_t BUF_SIZE = 4096;

int main() {
  AIO::run_in_new([](AIO::BasicScheduler *scheduler) {
    auto fd1 = AIO::StreamFD::open(scheduler->io(), "/tmp/fifo1", std::ios_base::in);
    auto fd2 = AIO::StreamFD::open(scheduler->io(), "/tmp/fifo2", std::ios_base::in);
    std::cout << "FIFOs opened." << std::endl;

    auto read_fifo = scheduler->async([](AIO::StreamFD fd, std::string name) {
      while (true) {
        AIO::StreamFD::Octet buffer[BUF_SIZE];
        auto size = fd.read(buffer);
        if (size == 0)
          break;
        std::cout << name << ": "
                  << std::string_view{
                       reinterpret_cast<const char *>(buffer), reinterpret_cast<const char *>(buffer) + size
                     };
        std::cout.flush();
      }
      return fd;
    });

    auto fiber1 = read_fifo(std::move(fd1), "FIFO#1");
    auto fiber2 = read_fifo(std::move(fd2), "FIFO#2");
    std::cout << "Fibers started." << std::endl;

    fd1 = scheduler->await(std::move(fiber1));
    fd2 = scheduler->await(std::move(fiber2));

    std::move(fd1).close();
    std::move(fd2).close();
  });
}
