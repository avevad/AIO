#pragma once

#include <type_traits>
#include <variant>
#include <functional>

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

            void drop() &&;
            void set_consumer(auto &&fun);

            ~FutureBase();

            using Result = Res;

        private:
            using BoundBase = Bound<Derived, Promise<Res>>;
            using Consumer = std::move_only_function<MetaConsumerSignatureT<Res>>;
            using ResultSubstitute = MetaFutureResultSubstituteT<Res>;

            friend Promise<Res>;
            friend SimpleEventLoop;
            friend Derived;

            template<typename... AcceptRes>
            void accept(AcceptRes &&...res);

            std::optional<Consumer> consumer = std::nullopt;
            std::optional<ResultSubstitute> result = std::nullopt;
        };

        template<FutureResult Res, typename Derived>
        class PromiseBase : public Bound<Derived, Future<Res>> {
        public:
            PromiseBase() = default;

            PromiseBase(PromiseBase &&other) = default;
            PromiseBase &operator=(PromiseBase &&other) noexcept = default;

            template<typename... FulfillRes>
            void fulfill(FulfillRes &&...res);

            ~PromiseBase();

        private:
            using BoundBase = Bound<Derived, Future<Res>>;

            friend Future<Res>;
            friend FutureBase<Res, Future<Res>>;
            friend Derived;

            bool fulfilled = false;
        };

    } // namespace _impl

    template<FutureResult Res>
    class Future : public _impl::FutureBase<Res, Future<Res>> {
        using Base = _impl::FutureBase<Res, Future>;

    public:
        using Base::Base;

        template<typename AsyncFunctor, typename Res1 = typename std::invoke_result_t<AsyncFunctor, Res>::Result>
        Future<Res1> then(AsyncFunctor &&fun) && {
            Promise<Res1> promise;
            Future<Res1> future;
            AIO::bind(promise, future);

            auto consumer = [
                promise = std::move(promise), fun = std::forward<AsyncFunctor>(fun)
            ] (Res res) mutable {
                Future<Res1> future1 = fun(std::move(res));
                // TODO: fix this memleak by implementing "Future <-> Promise" bond more thoroughly
                // (i.e. moving consumer straight into Promise instead of placing it in the Future --
                // this provides symmetric way of handling such bond)
                auto *future1_detached = new Future<Res1>(std::move(future1));
                if constexpr (!std::is_void_v<Res1>) {
                    auto consumer1 = [promise = std::move(promise)] (Res1 res1) mutable {
                        promise.fulfill(std::move(res1));
                    };
                    future1_detached->set_consumer(std::move(consumer1));
                } else {
                    auto consumer1 = [promise = std::move(promise)] mutable {
                        promise.fulfill();
                    };
                    future1_detached->set_consumer(std::move(consumer1));
                }
            };
            // TODO: same as above
            auto *future_detached = new Future<Res>(std::move(*this));
            future_detached->set_consumer_impl(std::move(consumer));

            return future;
        }

    private:
        friend Base;

        template<typename AcceptRes>
        void accept_impl(AcceptRes &&res);

        void set_consumer_impl(auto &&fun);
    };

    template<>
    class Future<void> : public _impl::FutureBase<void, Future<void>> {
        using Base = _impl::FutureBase<void, Future>;

    public:
        using Base::Base;

        template<typename AsyncFunctor, typename Res1 = typename std::invoke_result_t<AsyncFunctor>::Result>
        Future<Res1> then(AsyncFunctor &&fun) && {
            Promise<Res1> promise;
            Future<Res1> future;
            AIO::bind(promise, future);

            auto consumer = [
                promise = std::move(promise), fun = std::forward<AsyncFunctor>(fun)
            ] () mutable {
                Future<Res1> future1 = fun();
                // TODO: fix this memleak by implementing "Future <-> Promise" bond more thoroughly
                // (i.e. moving consumer straight into Promise instead of placing it in the Future --
                // this provides symmetric way of handling such bond)
                auto *future1_detached = new Future<Res1>(std::move(future1));
                if constexpr (!std::is_void_v<Res1>) {
                    auto consumer1 = [promise = std::move(promise)] (Res1 res1) mutable {
                        promise.fulfill(std::move(res1));
                    };
                    future1_detached->set_consumer(std::move(consumer1));
                } else {
                    auto consumer1 = [promise = std::move(promise)] mutable {
                        promise.fulfill();
                    };
                    future1_detached->set_consumer(std::move(consumer1));
                }
            };
            // TODO: same as above
            auto *future_detached = new Future<void>(std::move(*this));
            future_detached->set_consumer_impl(std::move(consumer));

            return future;
        }

    private:
        friend Base;

        void accept_impl();

        void set_consumer_impl(auto &&fun);
    };

    template<FutureResult Res>
    class Promise : public _impl::PromiseBase<Res, Promise<Res>> {
        using Base = _impl::PromiseBase<Res, Promise>;

    public:
        using Base::Base;

    private:
        friend Base;

        template<typename FulfillRes>
        void fulfill_impl(FulfillRes &&res);
    };

    template<>
    class Promise<void> : public _impl::PromiseBase<void, Promise<void>> {
        using Base = _impl::PromiseBase<void, Promise>;

    public:
        using Base::Base;

    private:
        friend Base;

        void fulfill_impl();
    };
}

#include "aio_bits/future.tcc"