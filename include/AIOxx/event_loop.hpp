#pragma once

#include "coroutine.hpp"
#include "fd.hpp"
#include "future.hpp"
#include "io.hpp"

#include <chrono>
#include <queue>
#include <set>

namespace AIO {

class BasicEventLoop {
public:
  BasicEventLoop();

  BasicEventLoop(const BasicEventLoop &) = delete;
  BasicEventLoop(BasicEventLoop &&other) = delete;

  BasicEventLoop &operator=(const BasicEventLoop &) = delete;
  BasicEventLoop &operator=(BasicEventLoop &&other) = delete;

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

  IOTasksQueue::Handle register_system_fd(SystemFD fd, IOTasksQueue::TaskCallback callback);

  const StreamFD &std_in();
  const StreamFD &std_out();
  const StreamFD &std_err();

  ~BasicEventLoop();

private:
  using Task = std::move_only_function<void()>;
  using Fiber = Coroutine<void()>;
  using FiberPtr = std::unique_ptr<Fiber>;

  struct TimedTask {
    std::chrono::time_point<std::chrono::steady_clock> when;
    Task what;

    bool operator<(const TimedTask &task1) const;
  };

  // TODO: better startup mechanism
  template<typename MainFunctor>
  friend void run_in_new(MainFunctor &&main);

  void run();
  void stop();

  void resume_fiber(FiberPtr fiber);

  std::queue<Task> pending_tasks = {};
  std::multiset<TimedTask> pending_timed_tasks = {};
  IOTasksQueue pending_io_tasks = {};

  bool stopped = false;
  FiberPtr current_fiber = nullptr;

  StreamFD in, out, err;
};

} // namespace AIO

#include "aio_bits/event_loop.tcc"
