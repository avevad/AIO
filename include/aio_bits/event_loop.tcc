#pragma once

#include "event_loop.hpp"

namespace AIO {

    template<typename Functor, typename... Args>
    Future<std::invoke_result_t<Functor, Args...>> SimpleEventLoop::async_execute(Functor &&fun, Args &&...args) {
        using Res = std::invoke_result_t<Functor, Args...>;

        Future<Res> future;
        Promise<Res> promise;
        AIO::bind(future, promise);

        auto job = [fun = std::forward<Functor>(fun), args = std::tuple<Args...>(std::forward<Args>(args)...),
                    promise = std::move(promise)] mutable {
            if constexpr (!std::is_void_v<Res>) {
                promise.fulfill(std::apply(fun, args));
            } else {
                std::apply(fun, args);
                promise.fulfill();
            }
        };
        auto coro = std::make_shared<CoroutineHolder>(std::move(job));

        auto task = [this, coro = std::move(coro)] mutable { do_coroutine_step(std::move(coro)); };
        pending_tasks.push(std::move(task));

        return future;
    }

    template<typename Functor>
    auto SimpleEventLoop::async(Functor &&fun) {
        return [this, fun = std::forward<Functor>(fun)]<typename... Args>(Args &&...args) {
            return this->async_execute(fun, std::forward<Args>(args)...);
        };
    }

    template<typename Res>
    Res SimpleEventLoop::await(Future<Res> future) {
        if (!current_coro.has_value()) {
            assertion_failed("attempt to await() outside of event loop");
        }

        auto task = [this, coro = current_coro.value()] mutable { do_coroutine_step(std::move(coro)); };
        if constexpr (!std::is_void_v<Res>) {
            std::optional<Res> result = std::nullopt;
            auto consumer = [this, task = std::move(task), &result](Res &&res) mutable {
                result = std::move(res);
                pending_tasks.push(std::move(task));
            };
            future.set_consumer(consumer);

            current_coro.value()->wrapped.yield();

            return std::move(result.value());
        } else {
            auto consumer = [this, task = std::move(task)]() mutable { pending_tasks.push(std::move(task)); };
            future.set_consumer(consumer);

            current_coro.value()->wrapped.yield();

            return;
        }
    }

    template<typename Functor>
    SimpleEventLoop::CoroutineHolder::CoroutineHolder(Functor fun) : wrapped(std::move(fun)) {
    }

    inline void SimpleEventLoop::do_coroutine_step(std::shared_ptr<CoroutineHolder> coro) {
        if (current_coro.has_value()) {
            assertion_failed("recursive do_coroutine_step() call");
        }
        current_coro = std::move(coro);
        current_coro.value()->wrapped.resume();
        current_coro = std::nullopt;
    }

    inline void run(const std::function<void(SimpleEventLoop &)> &main_function) {
        SimpleEventLoop loop;
        loop.async_execute([&loop, &main_function]() -> std::monostate {
                main_function(loop);
                return {};
            })
            .drop();
        loop.run();
    }

    inline void SimpleEventLoop::run() {
        while (!pending_tasks.empty()) {
            Task task = std::move(pending_tasks.front());
            pending_tasks.pop();

            try {
                task();
            } catch (...) {
                assertion_failed("exception in event loop task");
            }
        }
    }

} // namespace AIO
