#pragma once

#include <thread>
#include <utility>
#include "AIOxx/event_loop.hpp"

namespace AIO {

template<typename Functor, typename... Args>
Future<std::invoke_result_t<Functor, Args...>> BasicEventLoop::fiber(Functor &&fun, Args &&...args) {
  using Res = std::invoke_result_t<Functor, Args...>;
  auto [promise, future] = Contract<Res>();
  auto fiber = std::make_unique<Fiber>([this, fun = std::forward<Functor>(fun),
                                        args = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...),
                                        promise = std::move(promise)] mutable {
    auto invoker = [&]<typename... A>(A &&...a) mutable { return std::invoke(std::move(fun), std::forward<A>(a)...); };
    try {
      if constexpr (!std::is_void_v<Res>) {
        std::move(promise).fulfill(std::apply(invoker, std::move(args)));
      } else {
        std::apply(invoker, std::move(args));
        std::move(promise).fulfill();
      }
    } catch (...) {
      std::move(promise).fail_any(std::current_exception());
    }
    AIOXX_ASSUME(current_fiber != nullptr);

    // It is fiber's responsibility to deschedule itself, but we cannot destroy it right here as it is still alive.
    // Instead, we deschedule the fiber from the loop and schedule a task to dispose of the fiber after its completion.
    pending_tasks.push([fiber = std::move(current_fiber)] mutable { fiber.reset(); });
  });
  pending_tasks.push([this, fiber = std::move(fiber)] mutable { resume_fiber(std::move(fiber)); });
  return std::move(future);
}

template<typename Functor>
auto BasicEventLoop::async(Functor &&fun) {
  return [this, fun = std::forward<Functor>(fun)]<typename... Args>(Args &&...args) {
    return this->fiber(fun, std::forward<Args>(args)...);
  };
}

template<typename Res>
Res BasicEventLoop::await(Future<Res> future) {
  AIOXX_ASSUME(current_fiber != nullptr);

  // See comments below.
  Fiber &fiber = *current_fiber;

  Expected<Res> expected = std::unexpected<std::exception_ptr>(nullptr);
  std::move(future)
    .map_expected(
      [this, fiber = std::move(current_fiber),
       &expected](std::expected<Res, std::exception_ptr> expected1) mutable -> std::expected<void, std::exception_ptr> {
        expected = std::move(expected1);
        pending_tasks.push([this, fiber = std::move(fiber)] mutable { resume_fiber(std::move(fiber)); });
        return {};
      }
    )
    .detach();

  // Future consumer will live until executed once and the fiber will be held at least to this point.
  // Then the fiber will be moved into queue and by that means will live until resumed.
  // However, the consumer can be executed immediately, so TODO - examine fiber lifetime more carefully at this moment:
  fiber.yield();

  if (!expected.has_value())
    std::rethrow_exception(expected.error());

  if constexpr (!std::is_void_v<Res>)
    return std::move(*expected);
  else
    return;
}

template<typename Rep, typename Period>
Future<void> BasicEventLoop::timeout(const std::chrono::duration<Rep, Period> &duration) {
  return deadline(
    std::chrono::steady_clock::now() +
    std::chrono::duration_cast<
      std::chrono::steady_clock::duration, std::chrono::steady_clock::rep, std::chrono::steady_clock::period>(duration)
  );
}

template<typename MainFunctor>
void run_in_new(MainFunctor &&main) {
  BasicEventLoop loop;

  auto main_executed = loop.async([loop = &loop, main = std::forward<MainFunctor>(main)] { main(loop); });
  auto exception_caught = loop.async([](const std::exception &e) { panic("unhandled exception in main function", e); });
  auto anything_caught =
    loop.async([](const std::exception_ptr &) { panic("unhandled unknown exception in main function"); });
  auto loop_stopped = loop.async([loop = &loop] { loop->stop(); });

  main_executed()
    .template except<std::exception>(exception_caught)
    .except_any(anything_caught)
    .then(loop_stopped)
    .detach();

  loop.run();
}

inline bool BasicEventLoop::TimedTask::operator<(const TimedTask &task1) const {
  return when < task1.when;
}

inline BasicEventLoop::BasicEventLoop()
    : in(StreamFD::steal_from_system(*this, 0)), out(StreamFD::steal_from_system(*this, 1)),
      err(StreamFD::steal_from_system(*this, 2)) {
}

inline void BasicEventLoop::yield() {
  auto [promise, future] = AIO::Contract<void>();
  std::move(promise).fulfill();
  await(std::move(future));
}

inline void BasicEventLoop::stop() {
  stopped = true;
  yield();
  AIOXX_UNREACHABLE;
}

inline void BasicEventLoop::resume_fiber(FiberPtr fiber) {
  AIOXX_ASSUME(current_fiber == nullptr);
  current_fiber = std::move(fiber);
  current_fiber->resume();
  AIOXX_ASSUME(current_fiber == nullptr);
}

inline const StreamFD &BasicEventLoop::std_in() {
  return in;
}

inline const StreamFD &BasicEventLoop::std_out() {
  return out;
}

inline const StreamFD &BasicEventLoop::std_err() {
  return err;
}

inline Future<void> BasicEventLoop::deadline(const std::chrono::time_point<std::chrono::steady_clock> &time) {
  auto [promise, future] = Contract<void>();
  auto task = [promise = std::move(promise)] mutable { std::move(promise).fulfill(); };
  pending_timed_tasks.emplace(time, std::move(task));
  return std::move(future);
}

inline IOTasksQueue::Handle BasicEventLoop::register_system_fd(SystemFD fd, IOTasksQueue::TaskCallback callback) {
  return pending_io_tasks.push(fd, std::move(callback));
}

inline void BasicEventLoop::run() {
  try {
    while (!stopped) {
      // Check regular tasks that are available unconditionally
      if (!pending_tasks.empty()) {
        Task task = std::move(pending_tasks.front());
        pending_tasks.pop();
        task();
        continue;
      }

      // Check timed tasks that are already available right now
      auto now = std::chrono::steady_clock::now();
      if (!pending_timed_tasks.empty() && pending_timed_tasks.begin()->when <= now) {
        TimedTask task = std::move(pending_timed_tasks.extract(pending_timed_tasks.begin()).value());
        task.what();
        continue;
      }

      // Check I/O tasks (which would probably block)
      if (!pending_io_tasks.is_empty()) {
        std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline = std::nullopt;
        if (!pending_timed_tasks.empty()) {
          deadline = pending_timed_tasks.begin()->when;
        }
        auto maybe_task = pending_io_tasks.poll(deadline);
        if (maybe_task.has_value()) {
          maybe_task.value()();
          continue;
        }
      }

      // Check timed tasks (which would probably block)
      if (!pending_timed_tasks.empty()) {
        TimedTask task = std::move(pending_timed_tasks.extract(pending_timed_tasks.begin()).value());
        std::this_thread::sleep_until(task.when);
        task.what();
        continue;
      }

      break;
    }
  } catch (std::exception &e) {
    panic("exception in event loop", e);
  } catch (...) {
    panic("unknown exception in event loop");
  }
}

inline BasicEventLoop::~BasicEventLoop() {
  for (auto *fd : {&in, &out, &err}) {
    (void) std::move(*fd).release_to_system();
  }
}

} // namespace AIO
