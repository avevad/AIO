#pragma once

#include "future.hpp"

namespace AIO::_impl {

    template<FutureResult Res, typename Derived>
    FutureBase<Res, Derived>::~FutureBase() {
        if (!awaited) {
            assertion_failed("destroying non-awaited future");
        }
    }

    template<FutureResult Res, typename Derived>
    FutureBase<Res, Derived> &FutureBase<Res, Derived>::operator=(FutureBase &&other) noexcept {
        result = std::move(other.result);
        other.result.reset();

        awaited = other.awaited;
        other.awaited = true;

        return *this;
    }

    template<FutureResult Res, typename Derived>
    FutureBase<Res, Derived>::FutureBase(FutureBase &&other) noexcept: result(std::move(other.result)), awaited(other.awaited) {
        other.result.reset();
        other.awaited = true;
    }

    template<FutureResult Res, typename Derived>
    PromiseBase<Res, Derived>::~PromiseBase() {
        if (!fulfilled) {
            assertion_failed("destroying non-fulfilled promise");
        }
    }

    template<FutureResult Res, typename Derived>
    PromiseBase<Res, Derived> &PromiseBase<Res, Derived>::operator=(PromiseBase &&other) noexcept {
        consumer = std::move(other.consumer);
        other.consumer.reset();

        fulfilled = other.fulfilled;
        other.fulfilled = true;

        return *this;
    }

    template<FutureResult Res, typename Derived>
    PromiseBase<Res, Derived>::PromiseBase(PromiseBase &&other) noexcept: consumer(std::move(other.consumer)), fulfilled(other.fulfilled) {
        other.consumer.reset();
        other.fulfilled = true;
    }

} // namespace AIO::_impl

namespace AIO {

    template<FutureResult Res>
    template<typename AsyncFunctor, typename Res1>
    Future<Res1> Future<Res>::then(AsyncFunctor &&fun) && {
        Promise<Res1> promise;
        Future<Res1> future;
        AIO::bind(promise, future);

        auto consumer = [promise = std::move(promise), fun = std::forward<AsyncFunctor>(fun)](Res res) mutable {
            Future<Res1> future1 = fun(std::move(res));
            // TODO: fix this memleak by implementing "Future <-> Promise" bond more thoroughly
            // (i.e. moving consumer straight into Promise instead of placing it in the Future --
            // this provides symmetric way of handling such bond)
            auto *future1_detached = new Future<Res1>(std::move(future1));
            if constexpr (!std::is_void_v<Res1>) {
                auto consumer1 = [promise = std::move(promise)](Res1 res1) mutable {
                    promise.fulfill(std::move(res1));
                };
                future1_detached->set_consumer(std::move(consumer1));
            } else {
                auto consumer1 = [promise = std::move(promise)] mutable { promise.fulfill(); };
                future1_detached->set_consumer(std::move(consumer1));
            }
        };
        // TODO: same as above
        auto *future_detached = new Future<Res>(std::move(*this));
        future_detached->set_consumer_impl(std::move(consumer));

        return future;
    }

    template<typename AsyncFunctor, typename Res1>
    Future<Res1> Future<void>::then(AsyncFunctor &&fun) && {
        Promise<Res1> promise;
        Future<Res1> future;
        AIO::bind(promise, future);

        auto consumer = [promise = std::move(promise), fun = std::forward<AsyncFunctor>(fun)]() mutable {
            Future<Res1> future1 = fun();
            // TODO: fix this memleak by implementing "Future <-> Promise" bond more thoroughly
            // (i.e. moving consumer straight into Promise instead of placing it in the Future --
            // this provides symmetric way of handling such bond)
            auto *future1_detached = new Future<Res1>(std::move(future1));
            if constexpr (!std::is_void_v<Res1>) {
                auto consumer1 = [promise = std::move(promise)](Res1 res1) mutable {
                    promise.fulfill(std::move(res1));
                };
                future1_detached->set_consumer(std::move(consumer1));
            } else {
                auto consumer1 = [promise = std::move(promise)] mutable { promise.fulfill(); };
                future1_detached->set_consumer(std::move(consumer1));
            }
        };
        // TODO: same as above
        auto *future_detached = new Future<void>(std::move(*this));
        future_detached->set_consumer_impl(std::move(consumer));

        return future;
    }

    template<typename Res, typename Res1>
    Future<bool> operator|(Future<Res> &&future, Future<Res1> &&future1) {
        Future<bool> result;
        auto promise = std::make_shared<Promise<bool>>();
        AIO::bind(result, *promise);

        auto *future_detached = new Future(std::move(future));
        future_detached->set_consumer([promise] (auto...) {
            promise->fulfill(true);
        });

        auto *future1_detached = new Future(std::move(future1));
        future1_detached->set_consumer([promise] (auto...) {
            promise->fulfill(false);
        });

        return result;
    }

} // namespace AIO
