#pragma once

#include "event_loop.hpp"

#include <bits/std_thread.h>
#include <bits/this_thread_sleep.h>

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
    template<typename Rep, typename Period>
    Future<void> SimpleEventLoop::timeout(const std::chrono::duration<Rep, Period> &duration) {
        return deadline(
        std::chrono::steady_clock::now() +
            std::chrono::duration_cast<
                std::chrono::steady_clock::duration,
                std::chrono::steady_clock::rep,
                std::chrono::steady_clock::period
            >(duration)
        );
    }

    template<typename Functor>
    SimpleEventLoop::CoroutineHolder::CoroutineHolder(Functor fun) : wrapped(std::move(fun)) {
    }

    inline bool SimpleEventLoop::TimedTask::operator<(const TimedTask &task1) const {
        return when < task1.when;
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

    inline Future<void> SimpleEventLoop::deadline(const std::chrono::time_point<std::chrono::steady_clock> &time) {
        Future<void> future;
        Promise<void> promise;
        AIO::bind(future, promise);

        auto task = [promise = std::move(promise)] mutable { promise.fulfill(); };
        pending_timed_tasks.emplace(time, std::move(task));

        return future;
    }

    inline Future<IOEvent::Types> SimpleEventLoop::event(IOEvent event) {
        Future<IOEvent::Types> future;
        Promise<IOEvent::Types> promise;
        AIO::bind(future, promise);

        pending_io_tasks.push_back({
            .iter = pending_io_tasks.end() /* stub */,
            .event = event,
            .callback = [] (auto) {} /* stub */
        });
        auto iter = --pending_io_tasks.end();
        auto *pending_task = &*iter;

        IOEvent::Callback callback = [
            this, promise = std::move(promise), pending_task
        ] (IOEvent::Types event_types) mutable {
            promise.fulfill(event_types);
            io_queue.deregister_event(pending_task->event.sys_fd);
            pending_io_tasks.erase(pending_task->iter);
        };

        pending_task->iter = iter;
        pending_task->callback = std::move(callback);

        io_queue.register_event(event, &pending_task->callback, true);

        return future;

    }

    inline void SimpleEventLoop::run() {
        try {
            while (true) {
                if (!pending_tasks.empty()) {
                    Task task = std::move(pending_tasks.front());
                    pending_tasks.pop();
                    task();
                    continue;
                }

                if (!pending_io_tasks.empty()) {
                    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline = std::nullopt;
                    if (!pending_timed_tasks.empty()) {
                        deadline = pending_timed_tasks.begin()->when;
                    }
                    io_queue.poll_event(deadline);
                    continue;
                }

                if (!pending_timed_tasks.empty()) {
                    TimedTask task = std::move(pending_timed_tasks.extract(pending_timed_tasks.begin()).value());
                    std::this_thread::sleep_until(task.when);
                    task.what();
                    continue;
                }

                break;
            }
        } catch (...) {
            assertion_failed("exception in event loop");
        }
    }

} // namespace AIO
