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
        BoundBase::operator=(std::move(other));

        result = std::move(other.result);
        other.result.reset();

        awaited = other.awaited;
        other.awaited = true;

        return *this;
    }

    template<FutureResult Res, typename Derived>
    void FutureBase<Res, Derived>::drop() && {
        std::move(*static_cast<Derived *>(this)).consume([] (auto...) {});
    }

    template<FutureResult Res, typename Derived>
    FutureBase<Res, Derived>::FutureBase(FutureBase &&other) noexcept: BoundBase(std::move(other)), awaited(other.awaited), result(std::move(other.result)) {
        other.result.reset();
        other.awaited = true;
    }

    template<FutureResult Res, typename Derived>
    PromiseBase<Res, Derived>::~PromiseBase() {
        if (!fulfilled) {
            issue_warning("destroying non-fulfilled promise");
        }
    }

    template<FutureResult Res, typename Derived>
    PromiseBase<Res, Derived> &PromiseBase<Res, Derived>::operator=(PromiseBase &&other) noexcept {
        BoundBase::operator=(std::move(other));

        consumer = std::move(other.consumer);
        other.consumer.reset();

        fulfilled = other.fulfilled;
        other.fulfilled = true;

        return *this;
    }

    template<FutureResult Res, typename Derived>
    PromiseBase<Res, Derived>::PromiseBase(PromiseBase &&other) noexcept: BoundBase(std::move(other)), fulfilled(other.fulfilled), consumer(std::move(other.consumer)) {
        other.consumer.reset();
        other.fulfilled = true;
    }

} // namespace AIO::_impl

namespace AIO {

    template<FutureResult Res>
    template<typename AsyncFunctor, typename Res1>
    Future<Res1> Future<Res>::then(AsyncFunctor &&fun) && {
        Future<Res1> future;
        Promise<Res1> promise;
        AIO::bind(promise, future);

        auto consumer = [promise = std::move(promise), fun = std::forward<AsyncFunctor>(fun)] (Res res) mutable {
            auto future1 = fun(std::move(res));
            auto consumer1 = [promise = std::move(promise)]<typename ...Res1Arg>(Res1Arg &&...res1) mutable {
                std::move(promise).fulfill(std::forward<Res1Arg>(res1)...);
            };
            std::move(future1).consume(std::move(consumer1));
        };
        std::move(*this).consume(std::move(consumer));

        return future;
    }

    template<FutureResult Res>
    template<typename ConsumerArg>
    void Future<Res>::consume(ConsumerArg &&consumer) && {
        if (!Base::BoundBase::was_bound()) {
            assertion_failed("future was not bound to any promise");
        }

        if (!Base::awaited) {
            Base::awaited = true;
        } else {
            assertion_failed("attempt to await already awaited future");
        }

        if (Base::result.has_value()) {
            consumer(std::move(Base::result.value()));
        } else if (Base::BoundBase::is_bound()) {
            Base::BoundBase::get_bound_obj().consumer.emplace(std::forward<ConsumerArg>(consumer));
        }
    }

    template<typename AsyncFunctor, typename Res1>
    Future<Res1> Future<void>::then(AsyncFunctor &&fun) && {
        Future<Res1> future;
        Promise<Res1> promise;
        AIO::bind(promise, future);

        auto consumer = [promise = std::move(promise), fun = std::forward<AsyncFunctor>(fun)] () mutable {
            auto future1 = fun();
            auto consumer1 = [promise = std::move(promise)]<typename ...Res1Arg>(Res1Arg &&...res1) mutable {
                std::move(promise).fulfill(std::forward<Res1Arg>(res1)...);
            };
            std::move(future1).consume(std::move(consumer1));
        };
        std::move(*this).consume(std::move(consumer));

        return future;
    }

    template<typename ConsumerArg>
    void Future<void>::consume(ConsumerArg &&consumer) && {
        if (!Base::BoundBase::was_bound()) {
            assertion_failed("future was not bound to any promise");
        }

        if (!Base::awaited) {
            Base::awaited = true;
        } else {
            assertion_failed("attempt to await already awaited future");
        }

        if (Base::result.has_value()) {
            consumer();
        } else {
            Base::BoundBase::get_bound_obj().consumer.emplace(std::forward<ConsumerArg>(consumer));
        }
    }

    template<FutureResult Res>
    template<typename ResArg>
    void Promise<Res>::fulfill(ResArg &&res) && {
        if (!Base::BoundBase::was_bound()) {
            assertion_failed("promise was not bound to any future");
        }

        if (!Base::fulfilled) {
            Base::fulfilled = true;
        } else {
            assertion_failed("attempt to fulfill already fulfilled promise");
        }

        if (Base::consumer.has_value()) {
            Base::consumer.value()(std::forward<ResArg>(res));
        } else {
            Base::BoundBase::get_bound_obj().result.emplace(std::forward<ResArg>(res));
        }
    }

    inline void Promise<void>::fulfill() && {
        if (!Base::BoundBase::was_bound()) {
            assertion_failed("promise was not bound to any future");
        }

        if (!Base::fulfilled) {
            Base::fulfilled = true;
        } else {
            assertion_failed("attempt to fulfill already fulfilled promise");
        }

        if (Base::consumer.has_value()) {
            Base::consumer.value()();
        } else {
            Base::BoundBase::get_bound_obj().result.emplace();
        }
    }

    template<typename Res, typename Res1>
    Future<bool> operator|(Future<Res> &&future, Future<Res1> &&future1) {
        Future<bool> result;
        auto promise = std::make_shared<Promise<bool>>();
        AIO::bind(result, *promise);

        std::move(future).consume([promise](auto...) { std::move(*promise).fulfill(true); });

        std::move(future1).consume([promise](auto...) { std::move(*promise).fulfill(false); });

        return result;
    }

} // namespace AIO
