#include "AIOxx/scheduler.hpp"

#include "AIOxx/stdio.hpp"

namespace AIO {

BasicScheduler *BasicScheduler::IO::scheduler() {
  return &parent;
}

void BasicScheduler::IO::watch(IOQueue::Handle *handle) {
  parent.pending_io_tasks.add(handle);
}

void BasicScheduler::IO::forget(IOQueue::Handle *handle) {
  parent.pending_io_tasks.erase(handle);
}

BasicScheduler::IO::IO(BasicScheduler &parent) : parent(parent) {
}

bool BasicScheduler::Timer::operator<(const Timer &timer) const {
  return when < timer.when;
}

BasicScheduler::BasicScheduler() : std_io(std::make_unique<StdIO>(StdIO::steal_from_system(fd_io))) {
}

void BasicScheduler::yield() {
  auto [promise, future] = AIO::Contract<void>();
  std::move(promise).fulfill();
  await(std::move(future));
}

BasicScheduler::IO &BasicScheduler::io() {
  return fd_io;
}

void BasicScheduler::stop() {
  AIOXX_ASSUME(current_fiber != nullptr);
  auto *fiber = current_fiber.get();
  available_tasks.emplace([this, fiber = std::move(current_fiber)] mutable {
    stopped = true;
    fiber->coro.kill();
    fiber.reset();
  });
  fiber->coro.yield();
}

void BasicScheduler::resume_fiber(Fiber fiber) {
  AIOXX_ASSUME(current_fiber == nullptr);
  current_fiber = std::move(fiber);
  current_fiber->coro.resume();
  AIOXX_ASSUME(current_fiber == nullptr);
}

const StreamFD &BasicScheduler::std_in() {
  return std_io->in;
}

const StreamFD &BasicScheduler::std_out() {
  return std_io->out;
}

const StreamFD &BasicScheduler::std_err() {
  return std_io->err;
}

Future<void> BasicScheduler::deadline(const std::chrono::time_point<std::chrono::steady_clock> &time) {
  auto [promise, future] = Contract<void>();
  auto task = [promise = std::move(promise)] mutable { std::move(promise).fulfill(); };
  pending_timed_tasks.emplace(time, std::move(task));
  return std::move(future);
}

void BasicScheduler::run() {
  try {
    while (!stopped) {
      { // Check pending tasks for immediate availability -- this is crucial for fairness guarantee.
        auto now = std::chrono::steady_clock::now();
        while (!pending_timed_tasks.empty() && pending_timed_tasks.begin()->when <= now) {
          Task task = std::move(pending_timed_tasks.extract(pending_timed_tasks.begin()).value().what);
          available_tasks.push(std::move(task));
        }
        if (auto task = pending_io_tasks.poll(now))
          available_tasks.push(std::move(*task));
      }

      // Execute first available task, if any.
      if (!available_tasks.empty()) {
        Task task = std::move(available_tasks.front());
        available_tasks.pop();
        task();
        continue;
      }

      { // Otherwise block on I/O and wait until anything happens...
        AIOXX_ASSUME(!pending_io_tasks.empty());
        auto deadline = pending_timed_tasks.empty() ? std::nullopt : std::optional{pending_timed_tasks.begin()->when};
        if (auto task = pending_io_tasks.poll(deadline))
          available_tasks.push(std::move(*task));
      }
    }
  } catch (...) {
    panic("unexpected exception in scheduler", std::current_exception());
  }
}

BasicScheduler::~BasicScheduler() {
  AIOXX_ASSUME(current_fiber == nullptr);
  std::move(*std_io).release_to_system();
}

} // namespace AIO
