#pragma once
#include <queue>

namespace AIO {
    class SimpleEventLoop;

    template<typename Res>
    concept FutureResult = !std::is_reference_v<Res>;

    template<FutureResult Res>
    class Promise;

    template<FutureResult Res>
    class Future : public Bound<Future<Res>, Promise<Res>> {
    public:
        Future() = default;
        Future(Future &&other) = default;
        Future& operator=(Future &&other) noexcept = default;

        void drop() && {
            BoundBase::unbind();
        }

        ~Future();

    private:
        using BoundBase = Bound<Future, Promise<Res>>;
        using Consumer = std::move_only_function<void(Res)>;

        friend Promise<Res>;
        friend SimpleEventLoop;

        template<typename AcceptRes>
        void accept(AcceptRes &&res) {
            if (consumer.has_value()) {
                consumer.value()(std::forward<AcceptRes>(res));
            } else {
                result = std::forward<AcceptRes>(res);
            }
        }

        void set_consumer(auto &&fun) {
            if (consumer.has_value()) {
                assertion_failed("attempt to reset consumer");
            }
            consumer.emplace(fun);
            if (result.has_value()) {
                consumer.value()(std::move(result.value()));
                result.reset();
            }
        }

        std::optional<Consumer> consumer = std::nullopt;
        std::optional<Res> result = std::nullopt;
    };

    template<FutureResult Res>
    class Promise : public Bound<Promise<Res>, Future<Res>> {
    public:
        Promise() = default;

        Promise(Promise &&other) = default;
        Promise& operator=(Promise &&other) noexcept = default;

        template<typename FulfillRes>
        void fulfill(FulfillRes &&res) {
            if (fulfilled) {
                assertion_failed("attempt to fulfill already fulfilled promise");
            }
            if (auto *future_ptr = BoundBase::get_bound_ptr()) {
                future_ptr->accept(std::forward<FulfillRes>(res));
            }
            fulfilled = true;
        }

        ~Promise() {
            if (!BoundBase::is_bound()) {
                return;
            }
            if (BoundBase::get_bound_ptr() && !fulfilled) {
                assertion_failed("destroying non-fulfilled promise");
            }
        }

    private:
        using BoundBase = Bound<Promise, Future<Res>>;

        friend Future<Res>;

        bool fulfilled = false;
    };

    template<FutureResult Res>
    Future<Res>::~Future() {
        if (!BoundBase::is_bound()) {
            return;
        }
        if ((BoundBase::get_bound_ptr() && !BoundBase::get_bound_obj().fulfilled) || result.has_value()) {
            assertion_failed("destroying non-awaited future");
        }
    }

    class SimpleEventLoop {
    public:
        SimpleEventLoop() = default;

        SimpleEventLoop(const SimpleEventLoop &) = delete;
        SimpleEventLoop(SimpleEventLoop &&other) = delete;

        SimpleEventLoop &operator=(const SimpleEventLoop &) = delete;
        SimpleEventLoop &operator=(SimpleEventLoop &&other) = delete;

        template<typename Functor, typename... Args>
        Future<std::invoke_result_t<Functor, Args...>> async_execute(Functor &&fun, Args &&...args) {
            using Res = std::invoke_result_t<Functor, Args...>;

            Future<Res> future;
            Promise<Res> promise;
            bind(future, promise);

            auto job = [
                fun = std::forward<Functor>(fun),
                args = std::tuple<Args...>(std::forward<Args>(args)...),
                promise = std::move(promise)
            ] mutable {
                promise.fulfill(std::apply(fun, args));
            };
            auto coro = std::make_shared<CoroutineHolder>(std::move(job));

            auto task = [this, coro = std::move(coro)] mutable  {
                do_coroutine_step(std::move(coro));
            };
            pending_tasks.push(std::move(task));

            return future;
        }

        template<typename Functor>
        auto async(Functor &&fun) {
            return [this, fun = std::forward<Functor>(fun)] <typename... Args> (Args &&...args) {
                return this->async_execute(fun, std::forward<Args>(args)...);
            };
        }

        template<typename Res>
        Res await(Future<Res> future) {
            if (!current_coro.has_value()) {
                assertion_failed("attempt to await() outside of event loop");
            }

            std::optional<Res> result = std::nullopt;

            auto task = [this, coro = current_coro.value()] mutable {
                do_coroutine_step(std::move(coro));
            };
            auto consumer = [this, task = std::move(task), &result] (Res &&res) mutable {
                result = std::move(res);
                pending_tasks.push(std::move(task));
            };
            future.set_consumer(consumer);

            current_coro.value()->wrapped.yield();

            return std::move(result.value());
        }

        void run();

    private:
        struct CoroutineHolder : std::enable_shared_from_this<CoroutineHolder> {
            template<typename Functor>
            explicit CoroutineHolder(Functor fun) : wrapped(std::move(fun)) {
            }

            Coroutine<void()> wrapped;
        };

        using Task = std::move_only_function<void()>;

        template<FutureResult Res>
        friend class Future;

        void do_coroutine_step(std::shared_ptr<CoroutineHolder> coro) {
            if (current_coro.has_value()) {
                assertion_failed("recursive invoke_coroutine() call");
            }
            current_coro = std::move(coro);
            current_coro.value()->wrapped.resume();
            current_coro = std::nullopt;
        }

        std::queue<Task> pending_tasks = {};
        std::optional<std::shared_ptr<CoroutineHolder>> current_coro = std::nullopt;
    };

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

    void run(const std::function<void(SimpleEventLoop &)> &main_function) {
        SimpleEventLoop loop;
        loop.async_execute([&loop, &main_function] () -> std::monostate {
            main_function(loop);
            return {};
        }).drop();
        loop.run();
    }
} // namespace AIO
