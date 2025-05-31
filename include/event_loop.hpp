#pragma once

#include "future.hpp"
#include "util.hpp"

#include <chrono>
#include <queue>
#include <set>
#include <utility>
#include <variant>

namespace AIO {
    class SimpleEventLoop {
    public:
        SimpleEventLoop() = default;

        SimpleEventLoop(const SimpleEventLoop &) = delete;
        SimpleEventLoop(SimpleEventLoop &&other) = delete;

        SimpleEventLoop &operator=(const SimpleEventLoop &) = delete;
        SimpleEventLoop &operator=(SimpleEventLoop &&other) = delete;

        template<typename Functor, typename... Args>
        Future<std::invoke_result_t<Functor, Args...>> async_execute(Functor &&fun, Args &&...args);

        template<typename Functor>
        auto async(Functor &&fun);

        template<typename Res>
        Res await(Future<Res> future);

        template<typename Rep, typename Period>
        Future<void> sleep_for(const std::chrono::duration<Rep, Period> &duration);

        Future<void> sleep_until(const std::chrono::time_point<std::chrono::steady_clock> &time);

        void run();

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

        template<FutureResult Res>
        friend class Future;

        void do_coroutine_step(std::shared_ptr<CoroutineHolder> coro);

        std::queue<Task> pending_tasks = {};
        std::multiset<TimedTask> pending_timed_tasks = {};
        std::optional<std::shared_ptr<CoroutineHolder>> current_coro = std::nullopt;
    };

    void run(const std::function<void(SimpleEventLoop &)> &main_function);
} // namespace AIO

#include "aio_bits/event_loop.tcc"
