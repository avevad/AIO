#pragma once

#include "AIOxx/event_loop.hpp"

#include <thread>

namespace AIO {

    template<typename Functor, typename... Args>
    Future<std::invoke_result_t<Functor, Args...>> BasicEventLoop::execute(Functor &&fun, Args &&...args) {
        using Res = std::invoke_result_t<Functor, Args...>;

        Future<Res> future;
        Promise<Res> promise;
        AIO::bind(future, promise);

        auto job = [fun = std::forward<Functor>(fun),
                    args = std::tuple<Args...>(std::forward<Args>(args)...),
                    promise = std::move(promise)] mutable {
            try {
                if constexpr (!std::is_void_v<Res>) {
                    std::move(promise).fulfill(std::apply(fun, std::move(args)));
                } else {
                    std::apply(fun, std::move(args));
                    std::move(promise).fulfill();
                }
            } catch (...) {
                std::move(promise).fail(std::current_exception());
            }
        };
        auto coro = std::make_shared<CoroutineHolder>(std::move(job));

        auto task = [this, coro = std::move(coro)] mutable { do_coroutine_step(std::move(coro)); };
        pending_tasks.push(std::move(task));

        return future;
    }

    template<typename Functor>
    auto BasicEventLoop::async(Functor &&fun) {
        return [this, fun = std::forward<Functor>(fun)]<typename... Args>(Args &&...args) /* [[nodiscard]] */ {
            // TODO: use true-nodiscard functor (object of a class) ------------------------------^
            return this->execute(fun, std::forward<Args>(args)...);
        };
    }

    template<typename Res>
    Res BasicEventLoop::await(Future<Res> future) {
        if (!current_coro.has_value()) {
            assertion_failed("attempt to await() outside of event loop");
        }

        auto task = [this, coro = current_coro.value()] mutable { do_coroutine_step(std::move(coro)); };

        std::optional<WrappedResult<Res>> result = std::nullopt;
        std::exception_ptr error = nullptr;
        auto consumer = [this, task = std::move(task), &result, &error](MaybeResult<Res> maybe_res) mutable {
            if (auto *res = std::get_if<WrappedResult<Res>>(&maybe_res)) {
                result.emplace(std::move(*res));
            } else {
                error = std::get<std::exception_ptr>(maybe_res);
            }
            pending_tasks.push(std::move(task));
        };
        std::move(future).consume(std::move(consumer));

        current_coro.value()->wrapped.yield();

        if (error) [[unlikely]] {
            std::rethrow_exception(error);
        }

        return result.value().move_out();
    }
    template<typename Rep, typename Period>
    Future<void> BasicEventLoop::timeout(const std::chrono::duration<Rep, Period> &duration) {
        return deadline(
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<
                std::chrono::steady_clock::duration, std::chrono::steady_clock::rep, std::chrono::steady_clock::period>(
                duration));
    }

    template<typename Functor>
    BasicEventLoop::CoroutineHolder::CoroutineHolder(Functor fun) : wrapped(std::move(fun)) {
    }

    inline bool BasicEventLoop::TimedTask::operator<(const TimedTask &task1) const {
        return when < task1.when;
    }

    inline void BasicEventLoop::do_coroutine_step(std::shared_ptr<CoroutineHolder> coro) {
        if (current_coro.has_value()) {
            assertion_failed("recursive do_coroutine_step() call");
        }
        current_coro = std::move(coro);
        current_coro.value()->wrapped.resume();
        current_coro = std::nullopt;
    }

    inline void run(const std::function<void(BasicEventLoop &)> &main_function) {
        BasicEventLoop loop;
        auto loop_execute = loop.async([&loop, &main_function] { main_function(loop); });
        auto loop_stop = loop.async([&loop] { loop.stop(); });

        loop_execute().then(loop_stop).detach();
        loop.run();
    }

    inline BasicEventLoop::BasicEventLoop()
        : std_in(StreamFD::steal_from_system(*this, 0)), std_out(StreamFD::steal_from_system(*this, 1)),
          std_err(StreamFD::steal_from_system(*this, 2)) {
    }

    inline void BasicEventLoop::yield() {
        if (!current_coro) {
            assertion_failed("attempt to yield outside the event loop");
        }
        auto task = [this, coro = current_coro.value()] mutable { do_coroutine_step(std::move(coro)); };
        pending_tasks.emplace(std::move(task));
        current_coro.value()->wrapped.yield();
    }

    inline void BasicEventLoop::stop() {
        stopped = true;
        yield();
        assertion_failed("event loop stop trap");
    }

    inline Future<void> BasicEventLoop::deadline(const std::chrono::time_point<std::chrono::steady_clock> &time) {
        Future<void> future;
        Promise<void> promise;
        AIO::bind(future, promise);

        auto task = [promise = std::move(promise)] mutable { std::move(promise).fulfill(); };
        pending_timed_tasks.emplace(time, std::move(task));

        return future;
    }

    inline Future<void> BasicEventLoop::forever() {
        return execute([this] {
            Future<void> future;
            Promise<void> promise;
            bind(future, promise);
            await(std::move(future));
        });
    }

    inline IOTasksQueue::Handle BasicEventLoop::register_system_fd(SystemFD fd, IOTasksQueue::TaskCallback callback) {
        return pending_io_tasks.push(fd, std::move(callback));
    }

    inline void BasicEventLoop::run() {
        try {
            // TODO: check scheduling order, something seems a bit off here
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
                if (!pending_io_tasks.is_empty()) {
                    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline = std::nullopt;
                    if (!pending_timed_tasks.empty()) {
                        deadline = pending_timed_tasks.begin()->when;
                    }
                    auto maybe_task = pending_io_tasks.poll(deadline);
                    if (maybe_task.has_value()) {
                        maybe_task.value()();
                        continue;
                    }
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
        } catch (std::exception &e) {
            assertion_failed("exception in event loop", e);
        } catch (...) {
            assertion_failed("unknown exception in event loop");
        }
    }

    inline BasicEventLoop::~BasicEventLoop() {
        (void) std::move(const_cast<StreamFD &>(std_in)).release_to_system();
        (void) std::move(const_cast<StreamFD &>(std_out)).release_to_system();
        (void) std::move(const_cast<StreamFD &>(std_err)).release_to_system();
    }

} // namespace AIO
