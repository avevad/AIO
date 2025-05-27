#pragma once
#include <queue>
#include <variant>
#include <utility>

#include "util.hpp"
#include "future.hpp"

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

        void run();

    private:
        struct CoroutineHolder : std::enable_shared_from_this<CoroutineHolder> {
            template<typename Functor>
            explicit CoroutineHolder(Functor fun);

            Coroutine<void()> wrapped;
        };

        using Task = std::move_only_function<void()>;

        template<FutureResult Res>
        friend class Future;

        void do_coroutine_step(std::shared_ptr<CoroutineHolder> coro);

        std::queue<Task> pending_tasks = {};
        std::optional<std::shared_ptr<CoroutineHolder>> current_coro = std::nullopt;
    };

    void run(const std::function<void(SimpleEventLoop &)> &main_function);
} // namespace AIO

#include "aio_bits/event_loop.tcc"