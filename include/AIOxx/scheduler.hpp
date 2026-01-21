#pragma once

#include "coroutine.hpp"
#include "fd.hpp"
#include "future.hpp"
#include "io.hpp"

#include <chrono>
#include <queue>
#include <set>

namespace AIO {

class BasicScheduler {
public:
  BasicScheduler();

  BasicScheduler(const BasicScheduler &) = delete;
  BasicScheduler(BasicScheduler &&other) = delete;

  BasicScheduler &operator=(const BasicScheduler &) = delete;
  BasicScheduler &operator=(BasicScheduler &&other) = delete;

  template<typename Functor, typename... Args>
  Future<std::invoke_result_t<Functor, Args...>> fiber(Functor &&fun, Args &&...args);

  template<typename Rep, typename Period>
  Future<void> timeout(const std::chrono::duration<Rep, Period> &duration);

  Future<void> deadline(const std::chrono::time_point<std::chrono::steady_clock> &time);

  template<typename Functor>
  auto async(Functor &&fun);

  template<typename Res>
  Res await(Future<Res> future);

  void yield();

  const StreamFD &std_in();
  const StreamFD &std_out();
  const StreamFD &std_err();

  ~BasicScheduler();

private:
  using Fiber = Coroutine<void()>;
  using FiberPtr = std::unique_ptr<Fiber>;
  using Task = std::move_only_function<void()>;
  struct PendingTimedTask {
    std::chrono::time_point<std::chrono::steady_clock> when;
    Task what;

    bool operator<(const PendingTimedTask &task1) const;
  };

  // TODO: better startup mechanism
  template<typename MainFunctor>
  friend void run_in_new(MainFunctor &&main);

  void run();
  void stop();

  void resume_fiber(FiberPtr fiber);

  // TODO: better interface between FD and scheduler
  friend class FD;

  template<typename Callback>
  IOTasksQueue::Handle register_system_fd(SystemFD fd, Callback &&callback);

  std::queue<Task> available_tasks = {};
  std::multiset<PendingTimedTask> pending_timed_tasks = {};
  IOTasksQueue pending_io_tasks = {};

  bool stopped = false;
  FiberPtr current_fiber = nullptr;

  StreamFD in, out, err;
};

} // namespace AIO


// --------------------------------------------------
// -------------- TEMPLATE DEFINITIONS --------------
// --------------------------------------------------


namespace AIO {

template<typename Functor, typename... Args>
Future<std::invoke_result_t<Functor, Args...>> BasicScheduler::fiber(Functor &&fun, Args &&...args) {
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
      /*
    TODO: with proper implementation of Future cancelling this should look like this:
    } catch (const _impl::CoroutineKiller &) {*/
    } catch (...) {
      std::move(promise).fail_any(std::current_exception());
    }
    AIOXX_ASSUME(current_fiber != nullptr);

    // It is fiber's responsibility to deschedule itself, but we cannot destroy it right here as it is still alive.
    // Instead, we deschedule the fiber and schedule a task to dispose of the fiber after its completion.
    available_tasks.push([fiber = std::move(current_fiber)] mutable { fiber.reset(); });
  });
  available_tasks.push([this, fiber = std::move(fiber)] mutable { resume_fiber(std::move(fiber)); });
  return std::move(future);
}

template<typename Functor>
auto BasicScheduler::async(Functor &&fun) {
  return [this, fun = std::forward<Functor>(fun)]<typename... Args>(Args &&...args) {
    return this->fiber(fun, std::forward<Args>(args)...);
  };
}

template<typename Res>
Res BasicScheduler::await(Future<Res> future) {
  AIOXX_ASSUME(current_fiber != nullptr);

  // See comments below.
  Fiber &fiber = *current_fiber;

  Expected<Res> expected = std::unexpected<std::exception_ptr>(nullptr);
  std::move(future)
    .map_expected(
      [this, fiber = std::move(current_fiber),
       &expected](std::expected<Res, std::exception_ptr> expected1) mutable -> std::expected<void, std::exception_ptr> {
        expected = std::move(expected1);
        available_tasks.push([this, fiber = std::move(fiber)] mutable { resume_fiber(std::move(fiber)); });
        return {};
      }
    )
    .detach();

  // Future consumer will live until executed once and the fiber will be held at least to this point.
  // Then the fiber will be moved into queue and by that means will live until resumed.
  // However, the consumer can be executed immediately, so TODO -- examine fiber lifetime more carefully at this moment:
  fiber.yield();

  if (!expected.has_value())
    std::rethrow_exception(expected.error());

  if constexpr (!std::is_void_v<Res>)
    return std::move(*expected);
  else
    return;
}

template<typename Rep, typename Period>
Future<void> BasicScheduler::timeout(const std::chrono::duration<Rep, Period> &duration) {
  return deadline(
    std::chrono::steady_clock::now() +
    std::chrono::duration_cast<
      std::chrono::steady_clock::duration, std::chrono::steady_clock::rep, std::chrono::steady_clock::period>(duration)
  );
}

template<typename MainFunctor>
void run_in_new(MainFunctor &&main) {
  auto sched = std::make_unique<BasicScheduler>();

  auto main_executed = sched->async([sched = sched.get(), main = std::forward<MainFunctor>(main)] { main(sched); });
  auto exception_caught = sched->async([](auto err) { panic("unhandled exception", err); });
  auto sched_stopped = sched->async([sched = sched.get()] { sched->stop(); });

  main_executed().except_any(exception_caught).then(sched_stopped).detach();

  sched->run();
}

inline bool BasicScheduler::PendingTimedTask::operator<(const PendingTimedTask &task1) const {
  return when < task1.when;
}

inline BasicScheduler::BasicScheduler()
    : in(StreamFD::steal_from_system(this, 0)), out(StreamFD::steal_from_system(this, 1)),
      err(StreamFD::steal_from_system(this, 2)) {
}

inline void BasicScheduler::yield() {
  auto [promise, future] = AIO::Contract<void>();
  std::move(promise).fulfill();
  await(std::move(future));
}

inline void BasicScheduler::stop() {
  AIOXX_ASSUME(current_fiber != nullptr);
  auto &fiber = *current_fiber;
  available_tasks.emplace([this, fiber = std::move(current_fiber)] mutable {
    stopped = true;
    fiber->kill();
    fiber.reset();
  });
  fiber.yield();
}

inline void BasicScheduler::resume_fiber(FiberPtr fiber) {
  AIOXX_ASSUME(current_fiber == nullptr);
  current_fiber = std::move(fiber);
  current_fiber->resume();
  AIOXX_ASSUME(current_fiber == nullptr);
}

inline const StreamFD &BasicScheduler::std_in() {
  return in;
}

inline const StreamFD &BasicScheduler::std_out() {
  return out;
}

inline const StreamFD &BasicScheduler::std_err() {
  return err;
}

inline Future<void> BasicScheduler::deadline(const std::chrono::time_point<std::chrono::steady_clock> &time) {
  auto [promise, future] = Contract<void>();
  auto task = [promise = std::move(promise)] mutable { std::move(promise).fulfill(); };
  pending_timed_tasks.emplace(time, std::move(task));
  return std::move(future);
}

template<typename Callback>
IOTasksQueue::Handle BasicScheduler::register_system_fd(SystemFD fd, Callback &&callback) {
  return pending_io_tasks.create(
    fd, [this, callback = std::forward<Callback>(callback)](IOTasksQueue::EventTypes e) mutable {
      available_tasks.push([callback, e] { callback(e); });
    }
  );
}

inline void BasicScheduler::run() {
  try {
    while (!stopped) {
      { // Check pending tasks for immediate availability -- this is crucial for fairness guarantee.
        auto now = std::chrono::steady_clock::now();
        while (!pending_timed_tasks.empty() && pending_timed_tasks.begin()->when <= now) {
          PendingTimedTask task = std::move(pending_timed_tasks.extract(pending_timed_tasks.begin()).value());
          available_tasks.push(std::move(task.what));
        }
        if (auto schedule = pending_io_tasks.poll(now))
          (*schedule)();
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
        if (auto schedule = pending_io_tasks.poll(deadline))
          (*schedule)();
      }
    }
  } catch (...) {
    panic("unexpected exception in scheduler", std::current_exception());
  }
}

inline BasicScheduler::~BasicScheduler() {
  AIOXX_ASSUME(current_fiber == nullptr);
  for (auto *fd : {&in, &out, &err}) {
    (void) std::move(*fd).release_to_system();
  }
}

} // namespace AIO
