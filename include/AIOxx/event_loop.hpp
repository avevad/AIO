#pragma once

#include "coroutine.hpp"
#include "fd.hpp"
#include "future.hpp"
#include "io.hpp"
#include "util.hpp"

#include <chrono>
#include <list>
#include <queue>
#include <set>
#include <utility>
#include <variant>

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

  const StreamFD &get_stdin();
  const StreamFD &get_stdout();
  const StreamFD &get_stderr();

  ~BasicEventLoop();

private:
  struct CoroutineHolder : std::enable_shared_from_this<CoroutineHolder> {
    template<typename Functor>
    explicit CoroutineHolder(Functor fun);

    Coroutine<void()> wrapped;
  };

  using Task = std::move_only_function<void()>;

  struct TimedTask {
    std::chrono::time_point<std::chrono::steady_clock> when;
    Task what;

    bool operator<(const TimedTask &task1) const;
  };

  // TODO: better startup mechanism
  template<typename MainFunctor>
  friend void run_in_new(MainFunctor &&main);

  void run();
  [[noreturn]] void stop();

  void do_coroutine_step(std::shared_ptr<CoroutineHolder> coro);

  std::queue<Task> pending_tasks = {};
  std::multiset<TimedTask> pending_timed_tasks = {};
  IOTasksQueue pending_io_tasks = {};

  bool stopped = false;
  std::optional<std::shared_ptr<CoroutineHolder>> current_coro = std::nullopt;

  std::optional<StreamFD> std_in = std::nullopt, std_out = std::nullopt, std_err = std::nullopt;
};

} // namespace AIO

#include "aio_bits/event_loop.tcc"
