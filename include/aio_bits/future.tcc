#pragma once

#include "future.hpp"

namespace AIO::_impl {

    template<FutureResult Res, typename Derived>
    void FutureBase<Res, Derived>::drop() && {
        BoundBase::unbind();
    }

    template<FutureResult Res, typename Derived>
    template<typename... AcceptRes>
    void FutureBase<Res, Derived>::accept(AcceptRes &&...res) {
        static_cast<Derived *>(this)->accept_impl(std::forward<AcceptRes>(res)...);
    }

    template<FutureResult Res, typename Derived>
    void FutureBase<Res, Derived>::set_consumer(auto &&fun) {
        static_cast<Derived *>(this)->set_consumer_impl(std::forward<decltype(fun)>(fun));
    }

    template<FutureResult Res, typename Derived>
    FutureBase<Res, Derived>::~FutureBase() {
        if (!BoundBase::is_bound()) {
            return;
        }
        // TODO: properly implement and enable these checks
        // if ((BoundBase::get_bound_ptr() && !BoundBase::get_bound_obj().fulfilled) || result.has_value()) {
        //    assertion_failed("destroying non-awaited future");
        // }
    }

    template<FutureResult Res, typename Derived>
    template<typename... FulfillRes>
    void PromiseBase<Res, Derived>::fulfill(FulfillRes &&...res) {
        static_cast<Derived *>(this)->fulfill_impl(std::forward<FulfillRes>(res)...);
    }

    template<FutureResult Res, typename Derived>
    PromiseBase<Res, Derived>::~PromiseBase() {
        if (!BoundBase::is_bound()) {
            return;
        }
        // TODO: properly implement and enable these checks
        // if (BoundBase::get_bound_ptr() && !fulfilled) {
        //    assertion_failed("destroying non-fulfilled promise");
        // }
    }

} // namespace AIO::_impl

namespace AIO {

    template<FutureResult Res>
    template<typename AcceptRes>
    void Future<Res>::accept_impl(AcceptRes &&res) {
        if (Base::consumer.has_value()) {
            Base::consumer.value()(std::forward<AcceptRes>(res));
        } else {
            Base::result = std::forward<AcceptRes>(res);
        }
    }

    template<FutureResult Res>
    void Future<Res>::set_consumer_impl(auto &&fun) {
        if (Base::consumer.has_value()) {
            assertion_failed("attempt to reset consumer");
        }
        Base::consumer.emplace(std::forward<decltype(fun)>(fun));
        if (Base::result.has_value()) {
            Base::consumer.value()(std::move(Base::result.value()));
            Base::result.reset();
        }
    }
    void Future<void>::set_consumer_impl(auto &&fun) {
        if (Base::consumer.has_value()) {
            assertion_failed("attempt to reset consumer");
        }
        Base::consumer.emplace(std::forward<decltype(fun)>(fun));
        if (Base::result.has_value()) {
            Base::consumer.value()();
            Base::result.reset();
        }
    }

    template<FutureResult Res>
    template<typename FulfillRes>
    void Promise<Res>::fulfill_impl(FulfillRes &&res) {
        // if (Base::fulfilled) {
        //     assertion_failed("attempt to fulfill already fulfilled promise");
        // }
        if (auto *future_ptr = Base::BoundBase::get_bound_ptr()) {
            future_ptr->accept(std::forward<FulfillRes>(res));
        }
        Base::fulfilled = true;
    }

    inline void Future<void>::accept_impl() {
        if (Base::consumer.has_value()) {
            Base::consumer.value()();
        } else {
            Base::result = std::monostate{};
        }
    }

    inline void Promise<void>::fulfill_impl() {
        // TODO: properly implement and enable these checks
        // if (Base::fulfilled) {
        //    assertion_failed("attempt to fulfill already fulfilled promise");
        // }
        if (auto *future_ptr = Base::BoundBase::get_bound_ptr()) {
            future_ptr->accept();
        }
        Base::fulfilled = true;
    }

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
