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
                std::move(promise).fulfill(std::apply(fun, args));
            } else {
                std::apply(fun, args);
                std::move(promise).fulfill();
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
            std::move(future).consume(std::move(consumer));

            current_coro.value()->wrapped.yield();

            return std::move(result.value());
        } else {
            auto consumer = [this, task = std::move(task)]() mutable { pending_tasks.push(std::move(task)); };
            std::move(future).consume(std::move(consumer));

            current_coro.value()->wrapped.yield();

            return;
        }
    }
    template<typename Rep, typename Period>
    Future<void> SimpleEventLoop::timeout(const std::chrono::duration<Rep, Period> &duration) {
        return deadline(
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<
                std::chrono::steady_clock::duration, std::chrono::steady_clock::rep, std::chrono::steady_clock::period>(
                duration));
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
        auto loop_execute = loop.async([&loop, &main_function] { main_function(loop); });
        auto loop_stop = loop.async([&loop] { loop.stop(); });

        loop_execute().then(loop_stop).drop();
        loop.run();
    }

    inline SimpleEventLoop::SimpleEventLoop()
        : std_in(StreamFD::steal_system(this, 0)), std_out(StreamFD::steal_system(this, 1)),
          std_err(StreamFD::steal_system(this, 2)) {
    }

    inline void SimpleEventLoop::yield() {
        if (!current_coro) {
            assertion_failed("attempt to yield outside the event loop");
        }
        auto task = [this, coro = current_coro.value()] mutable { do_coroutine_step(std::move(coro)); };
        pending_tasks.emplace(std::move(task));
        current_coro.value()->wrapped.yield();
    }

    inline void SimpleEventLoop::stop() {
        stopped = true;
        yield();
        assertion_failed("event loop stop trap");
    }

    inline Future<void> SimpleEventLoop::deadline(const std::chrono::time_point<std::chrono::steady_clock> &time) {
        Future<void> future;
        Promise<void> promise;
        AIO::bind(future, promise);

        auto task = [promise = std::move(promise)] mutable { std::move(promise).fulfill(); };
        pending_timed_tasks.emplace(time, std::move(task));

        return future;
    }

    inline Future<IOEvent::Types> SimpleEventLoop::event(IOEvent event) {
        Future<IOEvent::Types> future;
        Promise<IOEvent::Types> promise;
        AIO::bind(future, promise);

        pending_io_tasks.push_back({
            .iter = pending_io_tasks.end() /* stub */, .event = event, .callback = [](auto) {} /* stub */
        });
        auto iter = --pending_io_tasks.end();
        auto *pending_task = &*iter;

        IOEvent::Callback callback = [this, promise = std::move(promise),
                                      pending_task](IOEvent::Types event_types) mutable {
            std::move(promise).fulfill(event_types);
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
                if (!pending_io_tasks.empty()) {
                    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline = std::nullopt;
                    if (!pending_timed_tasks.empty()) {
                        deadline = pending_timed_tasks.begin()->when;
                    }
                    io_queue.poll_event(deadline);
                    continue;
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
        } catch (...) {
            assertion_failed("exception in event loop");
        }
    }

} // namespace AIO
