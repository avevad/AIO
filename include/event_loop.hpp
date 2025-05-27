#pragma once
#include <queue>
#include <variant>
#include <utility>

#include "util.hpp"

namespace AIO {
    class SimpleEventLoop;

    template<typename Res>
    concept FutureResult = !std::is_reference_v<Res>;

    template<FutureResult Res>
    class Promise;

    template<FutureResult Res>
    class Future;

    namespace _impl {
        template<FutureResult Res>
        struct MetaConsumerSignature {
            using Type = void(Res);
        };

        template<>
        struct MetaConsumerSignature<void> {
            using Type = void();
        };

        template<FutureResult Res>
        using MetaConsumerSignatureT = typename MetaConsumerSignature<Res>::Type;

        template<FutureResult Res>
        using MetaFutureResultSubstituteT = std::conditional_t<std::is_void_v<Res>, std::monostate, Res>;

        template<FutureResult Res, typename Derived>
        class FutureBase : public Bound<Derived, Promise<Res>> {
        public:
            FutureBase() = default;
            FutureBase(FutureBase &&other) = default;
            FutureBase &operator=(FutureBase &&other) noexcept = default;

            void drop() && {
                BoundBase::unbind();
            }

            ~FutureBase();

        private:
            using BoundBase = Bound<Derived, Promise<Res>>;
            using Consumer = std::move_only_function<MetaConsumerSignatureT<Res>>;
            using ResultSubstitute = MetaFutureResultSubstituteT<Res>;

            friend Promise<Res>;
            friend SimpleEventLoop;
            friend Derived;

            template<typename ...AcceptRes>
            void accept(AcceptRes &&...res) {
                static_cast<Derived *>(this)->accept_impl(std::forward<AcceptRes>(res)...);
            }

            void set_consumer(auto &&fun) {
                static_cast<Derived *>(this)->set_consumer_impl(std::forward<decltype(fun)>(fun));
            }

            std::optional<Consumer> consumer = std::nullopt;
            std::optional<ResultSubstitute> result = std::nullopt;
        };

        template<FutureResult Res, typename Derived>
        FutureBase<Res, Derived>::~FutureBase() {
            if (!BoundBase::is_bound()) {
                return;
            }
            if ((BoundBase::get_bound_ptr() && !BoundBase::get_bound_obj().fulfilled) || result.has_value()) {
                assertion_failed("destroying non-awaited future");
            }
        }

        template<FutureResult Res, typename Derived>
        class PromiseBase : public Bound<Derived, Future<Res>> {
        public:
            PromiseBase() = default;

            PromiseBase(PromiseBase &&other) = default;
            PromiseBase& operator=(PromiseBase &&other) noexcept = default;

            template<typename ...FulfillRes>
            void fulfill(FulfillRes &&...res) {
                static_cast<Derived *>(this)->fulfill_impl(std::forward<FulfillRes>(res)...);
            }

            ~PromiseBase() {
                if (!BoundBase::is_bound()) {
                    return;
                }
                if (BoundBase::get_bound_ptr() && !fulfilled) {
                    assertion_failed("destroying non-fulfilled promise");
                }
            }

        private:
            using BoundBase = Bound<Derived, Future<Res>>;

            friend Future<Res>;
            friend FutureBase<Res, Future<Res>>;
            friend Derived;

            bool fulfilled = false;
        };

    }

    template<FutureResult Res>
    class Future : public _impl::FutureBase<Res, Future<Res>> {
        using Base = _impl::FutureBase<Res, Future>;

    public:
        using Base::Base;

    private:
        friend Base;

        template<typename AcceptRes>
        void accept_impl(AcceptRes &&res) {
            if (Base::consumer.has_value()) {
                Base::consumer.value()(std::forward<AcceptRes>(res));
            } else {
                Base::result = std::forward<AcceptRes>(res);
            }
        }

        void set_consumer_impl(auto &&fun) {
            if (Base::consumer.has_value()) {
                assertion_failed("attempt to reset consumer");
            }
            Base::consumer.emplace(fun);
            if (Base::result.has_value()) {
                Base::consumer.value()(std::move(Base::result.value()));
                Base::result.reset();
            }
        }
    };

    template<>
    class Future<void> : public _impl::FutureBase<void, Future<void>> {
        using Base = _impl::FutureBase<void, Future>;

    public:
        using Base::Base;

    private:
        friend Base;

        void accept_impl() {
            if (Base::consumer.has_value()) {
                Base::consumer.value()();
            } else {
                Base::result = std::monostate {};
            }
        }

        void set_consumer_impl(auto &&fun) {
            if (Base::consumer.has_value()) {
                assertion_failed("attempt to reset consumer");
            }
            Base::consumer.emplace(fun);
            if (Base::result.has_value()) {
                Base::consumer.value()();
                Base::result.reset();
            }
        }
    };

    template<FutureResult Res>
    class Promise : public _impl::PromiseBase<Res, Promise<Res>> {
        using Base = _impl::PromiseBase<Res, Promise>;

    public:
        using Base::Base;

    private:
        friend Base;

        template<typename FulfillRes>
        void fulfill_impl(FulfillRes &&res) {
            if (Base::fulfilled) {
                assertion_failed("attempt to fulfill already fulfilled promise");
            }
            if (auto *future_ptr = Base::BoundBase::get_bound_ptr()) {
                future_ptr->accept(std::forward<FulfillRes>(res));
            }
            Base::fulfilled = true;
        }
    };

    template<>
    class Promise<void> : public _impl::PromiseBase<void, Promise<void>> {
        using Base = _impl::PromiseBase<void, Promise>;

    public:
        using Base::Base;

    private:
        friend Base;

        void fulfill_impl() {
            if (Base::fulfilled) {
                assertion_failed("attempt to fulfill already fulfilled promise");
            }
            if (auto *future_ptr = Base::BoundBase::get_bound_ptr()) {
                future_ptr->accept();
            }
            Base::fulfilled = true;
        }
    };


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
            AIO::bind(future, promise);

            auto job = [
                fun = std::forward<Functor>(fun),
                args = std::tuple<Args...>(std::forward<Args>(args)...),
                promise = std::move(promise)
            ] mutable {
                if constexpr (!std::is_void_v<Res>) {
                    promise.fulfill(std::apply(fun, args));
                } else {
                    std::apply(fun, args);
                    promise.fulfill();
                }
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

            auto task = [this, coro = current_coro.value()] mutable {
                do_coroutine_step(std::move(coro));
            };
            if constexpr (!std::is_void_v<Res>) {
                std::optional<Res> result = std::nullopt;
                auto consumer = [this, task = std::move(task), &result] (Res &&res) mutable {
                    result = std::move(res);
                    pending_tasks.push(std::move(task));
                };
                future.set_consumer(consumer);

                current_coro.value()->wrapped.yield();

                return std::move(result.value());
            } else {
                auto consumer = [this, task = std::move(task)] () mutable {
                    pending_tasks.push(std::move(task));
                };
                future.set_consumer(consumer);

                current_coro.value()->wrapped.yield();

                return;
            }
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

    inline void run(const std::function<void(SimpleEventLoop &)> &main_function) {
        SimpleEventLoop loop;
        loop.async_execute([&loop, &main_function] () -> std::monostate {
            main_function(loop);
            return {};
        }).drop();
        loop.run();
    }
} // namespace AIO
