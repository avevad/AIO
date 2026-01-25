#pragma once

#include "coroutine.hpp"
#include "future.hpp"
#include "io.hpp"

#include <chrono>
#include <queue>
#include <set>

namespace AIO {
struct StdIO;
class StreamFD;
class BasicScheduler {
public:
  class IO;

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

  IO &io();

  const StreamFD &std_in();
  const StreamFD &std_out();
  const StreamFD &std_err();

  ~BasicScheduler();

  class IO {
  public:
    IO(const IO &) = delete;
    IO(IO &&) noexcept = delete;
    IO &operator=(const IO &) = delete;
    IO &operator=(IO &&) = delete;

    [[nodiscard]] BasicScheduler *scheduler();

    void watch(IOQueue::Handle *handle);
    void forget(IOQueue::Handle *handle);

  private:
    friend BasicScheduler;
    explicit IO(BasicScheduler &parent);

    BasicScheduler &parent;
  };

private:
  using Fiber = Coroutine<void()>;
  using FiberPtr = std::unique_ptr<Fiber>;
  using Task = std::move_only_function<void()>;
  struct Timer {
    bool operator<(const Timer &timer) const;

    std::chrono::time_point<std::chrono::steady_clock> when;
    Task what;
  };

  // TODO: better startup mechanism
  template<typename MainFunctor>
  friend void run_in_new(MainFunctor &&main);

  void run();
  void stop();

  void resume_fiber(FiberPtr fiber);

  std::queue<Task> available_tasks = {};
  std::multiset<Timer> pending_timed_tasks = {};
  IOQueue pending_io_tasks = {};

  bool stopped = false;
  FiberPtr current_fiber = nullptr;

  IO fd_io{*this};
  std::unique_ptr<StdIO> std_io;
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
[[nodiscard]] Res BasicScheduler::await(Future<Res> future) {
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

} // namespace AIO
