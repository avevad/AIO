#pragma once

#include <thread>
#include <utility>
#include "AIOxx/event_loop.hpp"

namespace AIO {

template<typename Functor, typename... Args>
Future<std::invoke_result_t<Functor, Args...>> BasicEventLoop::fiber(Functor &&fun, Args &&...args) {
  using Res = std::invoke_result_t<Functor, Args...>;
  auto [promise, future] = Contract<Res>();
  auto coro = std::make_shared<CoroutineHolder>([fun = std::forward<Functor>(fun),
                                                 args = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...),
                                                 promise = std::move(promise)] mutable {
    try {
      if constexpr (!std::is_void_v<Res>) {
        std::move(promise).fulfill(
          std::apply([&](auto &...xs) { return std::invoke(fun, std::forward_like<Args>(xs)...); }, args)
        );
      } else {
        std::apply([&](auto &...xs) { std::invoke(fun, std::forward_like<Args>(xs)...); }, args);
        std::move(promise).fulfill();
      }
    } catch (...) {
      std::move(promise).fail_any(std::current_exception());
    }
  });
  pending_tasks.push([this, coro = std::move(coro)] mutable { do_coroutine_step(std::move(coro)); });
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
  AIOXX_ASSUME(current_coro.has_value());

  auto task = [this, coro = current_coro.value()] mutable { do_coroutine_step(std::move(coro)); };

  Expected<Res> expected = std::unexpected<std::exception_ptr>(nullptr);
  std::move(future)
    .map_expected([this, task = std::move(task), &expected](std::expected<Res, std::exception_ptr> expected1) mutable {
      expected = std::move(expected1);
      pending_tasks.push(std::move(task));
      return std::expected<void, std::exception_ptr>{};
    })
    .detach();

  current_coro.value()->wrapped.yield();

  if (!expected.has_value()) {
    std::rethrow_exception(expected.error());
  }

  if constexpr (!std::is_void_v<Res>) {
    return std::move(*expected);
  }
}

template<typename Rep, typename Period>
Future<void> BasicEventLoop::timeout(const std::chrono::duration<Rep, Period> &duration) {
  return deadline(
    std::chrono::steady_clock::now() +
    std::chrono::duration_cast<
      std::chrono::steady_clock::duration, std::chrono::steady_clock::rep, std::chrono::steady_clock::period>(duration)
  );
}

template<typename Functor>
BasicEventLoop::CoroutineHolder::CoroutineHolder(Functor fun) : wrapped(std::move(fun)) {
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

inline void BasicEventLoop::do_coroutine_step(std::shared_ptr<CoroutineHolder> coro) {
  AIOXX_ASSUME(!current_coro.has_value());
  current_coro = std::move(coro);
  current_coro.value()->wrapped.resume();
  current_coro = std::nullopt;
}

inline BasicEventLoop::BasicEventLoop() {
}

inline void BasicEventLoop::yield() {
  AIOXX_ASSUME(current_coro);
  auto task = [this, coro = current_coro.value()] mutable { do_coroutine_step(std::move(coro)); };
  pending_tasks.emplace(std::move(task));
  current_coro.value()->wrapped.yield();
}

inline void BasicEventLoop::stop() {
  stopped = true;
  yield();
  AIOXX_UNREACHABLE;
}

inline const StreamFD &BasicEventLoop::get_stdin() {
  if (!std_in.has_value()) {
    std_in.emplace(StreamFD::steal_from_system(*this, 0));
  }
  return std_in.value();
}

inline const StreamFD &BasicEventLoop::get_stdout() {
  if (!std_out.has_value()) {
    std_out.emplace(StreamFD::steal_from_system(*this, 1));
  }
  return std_out.value();
}

inline const StreamFD &BasicEventLoop::get_stderr() {
  if (!std_err.has_value()) {
    std_err.emplace(StreamFD::steal_from_system(*this, 2));
  }
  return std_err.value();
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
    // TODO: check scheduling order, something seems a bit off here
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
  for (auto *maybe_stream : {&std_in, &std_out, &std_err}) {
    if (maybe_stream->has_value()) {
      (void) std::move(maybe_stream->value()).release_to_system();
    }
  }
}

} // namespace AIO
