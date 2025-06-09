#pragma once

#include "coroutine.hpp"
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

    class SimpleEventLoop {
    public:
        SimpleEventLoop();

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
        Future<void> timeout(const std::chrono::duration<Rep, Period> &duration);

        Future<void> deadline(const std::chrono::time_point<std::chrono::steady_clock> &time);

        Future<IOEvent::Types> event(IOEvent event);

        void run();

        const StreamFD std_in, std_out, std_err;

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

        struct IOTask {
            std::list<IOTask>::iterator iter;
            IOEvent event;
            IOEvent::Callback callback;
        };

        template<FutureResult Res>
        friend class Future;

        void do_coroutine_step(std::shared_ptr<CoroutineHolder> coro);

        std::queue<Task> pending_tasks = {};
        std::multiset<TimedTask> pending_timed_tasks = {};
        std::list<IOTask> pending_io_tasks = {};

        std::optional<std::shared_ptr<CoroutineHolder>> current_coro = std::nullopt;
        IOQueue io_queue;
    };

    void run(const std::function<void(SimpleEventLoop &)> &main_function);

} // namespace AIO

#include "aio_bits/event_loop.tcc"
