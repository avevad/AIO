#pragma once

#include "AIOxx/future.hpp"

#include <memory>

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
    void FutureBase<Res, Derived>::detach() && {
        std::move(*static_cast<Derived *>(this)).consume([](MaybeResult<Res> maybe_res) {
            if (auto *error = std::get_if<std::exception_ptr>(&maybe_res)) [[unlikely]] {
                try {
                    std::rethrow_exception(*error);
                } catch (std::exception &e) {
                    issue_warning("unhandled error in detached future", e);
                } catch (...) {
                    issue_warning("unhandled unknown error in detached future");
                }
            }
        });
    }

    template<FutureResult Res, typename Derived>
    template<typename Error, typename AsyncFunctor>
    Future<Res> FutureBase<Res, Derived>::except(AsyncFunctor &&handler) {
        Future<Res> future;
        Promise<Res> promise;
        AIO::bind(promise, future);

        auto consumer = [
            promise = std::move(promise), handler = std::forward<AsyncFunctor>(handler)
        ] (const MaybeResult<Res> &maybe_res) mutable {
            if (auto *error = std::get_if<std::exception_ptr>(&maybe_res)) [[unlikely]] {
                try {
                    std::rethrow_exception(*error);
                } catch (Error &e) {
                    Future<Res> future1 = handler(e);
                    auto consumer1 = [promise = std::move(promise)](MaybeResult<Res> maybe_res1) mutable {
                        std::move(promise).propagate(maybe_res1);
                    };
                    std::move(future1).consume(std::move(consumer1));
                } catch (...) {
                    std::move(promise).fail(std::current_exception());
                }
            } else {
                if constexpr (std::is_void_v<Res>) {
                    std::move(promise).fulfill();
                } else {
                    std::move(promise).fulfill(std::move(std::get<WrappedResult<Res>>(maybe_res).obj));
                }
            }
        };
        std::move(*static_cast<Derived *>(this)).consume(std::move(consumer));

        return future;
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
    void PromiseBase<Res, Derived>::fail(std::exception_ptr error) && {
        if (!BoundBase::was_bound()) {
            assertion_failed("promise was not bound to any future");
        }

        if (!fulfilled) {
            fulfilled = true;
        } else {
            assertion_failed("attempt to fulfill already fulfilled promise");
        }

        if (consumer.has_value()) {
            consumer.value()(std::move(error));
        } else {
            BoundBase::get_bound_obj().error = std::move(error);
        }
    }

    template<FutureResult Res, typename Derived>
    void PromiseBase<Res, Derived>::propagate(MaybeResult<Res> maybe_res) && {
        if (auto *res = std::get_if<WrappedResult<Res>>(&maybe_res)) {
            if constexpr (std::is_void_v<Res>) {
                std::move(*static_cast<Derived *>(this)).fulfill();
            } else {
                std::move(*static_cast<Derived *>(this)).fulfill(std::move(res->obj));
            }
        } else {
            std::move(*this).fail(std::get<std::exception_ptr>(maybe_res));
        }
    }

    template<FutureResult Res, typename Derived>
    bool PromiseBase<Res, Derived>::is_fulfilled() {
        return fulfilled;
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

        auto consumer = [
            promise = std::move(promise), fun = std::forward<AsyncFunctor>(fun)
        ] (const MaybeResult<Res> &maybe_res) mutable {
            if (auto *res = std::get_if<WrappedResult<Res>>(&maybe_res)) {
                Future<Res1> future1 = fun(std::move(res->obj));
                auto consumer1 = [promise = std::move(promise)](MaybeResult<Res1> maybe_res1) mutable {
                    std::move(promise).propagate(std::move(maybe_res1));
                };
                std::move(future1).consume(std::move(consumer1));
            } else {
                std::move(promise).fail(std::get<std::exception_ptr>(maybe_res));
            }
        };
        std::move(*this).consume(std::move(consumer));

        return future;
    }

    template<FutureResult Res>
    template<typename Functor, typename Res1>
    Future<Res1> Future<Res>::map(Functor &&fun) && {
        Future<Res1> future;
        Promise<Res1> promise;
        AIO::bind(promise, future);

        auto consumer = [
            promise = std::move(promise), fun = std::forward<Functor>(fun)
        ] (const MaybeResult<Res> &maybe_res) mutable {
            if (auto *res = std::get_if<WrappedResult<Res>>(&maybe_res)) {
                try {
                    if constexpr (std::is_void_v<Res1>) {
                        fun(std::move(res->obj));
                        std::move(promise).fulfill();
                    } else {
                        std::move(promise).fulfill(fun(std::move(res->obj)));
                    }
                } catch (...) {
                    std::move(promise).fail(std::current_exception());
                }
            } else {
                std::move(promise).fail(std::get<std::exception_ptr>(maybe_res));
            }
        };
        std::move(*this).consume(std::move(consumer));

        return future;
    }

    template<FutureResult Res>
    void Future<Res>::consume(typename Base::Consumer consumer) && {
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
        } else if (Base::error) {
            consumer(Base::error);
        } if (Base::BoundBase::is_bound()) {
            Base::BoundBase::get_bound_obj().consumer = std::move(consumer);
        }
    }

    template<typename AsyncFunctor, typename Res1>
    Future<Res1> Future<void>::then(AsyncFunctor &&fun) && {
        Future<Res1> future;
        Promise<Res1> promise;
        AIO::bind(promise, future);

        auto consumer = [promise = std::move(promise),
                         fun = std::forward<AsyncFunctor>(fun)](const MaybeResult<void> &maybe_res) mutable {
            if (std::get_if<WrappedResult<void>>(&maybe_res)) {
                Future<Res1> future1 = fun();
                auto consumer1 = [promise = std::move(promise)](MaybeResult<Res1> maybe_res1) mutable {
                    std::move(promise).propagate(std::move(maybe_res1));
                };
                std::move(future1).consume(std::move(consumer1));
            } else {
                std::move(promise).fail(std::get<std::exception_ptr>(maybe_res));
            }
        };
        std::move(*this).consume(std::move(consumer));

        return future;
    }

    template<typename Functor, typename Res1>
    Future<Res1> Future<void>::map(Functor &&fun) && {
        Future<Res1> future;
        Promise<Res1> promise;
        AIO::bind(promise, future);

        auto consumer = [
            promise = std::move(promise), fun = std::forward<Functor>(fun)
        ] (const MaybeResult<void> &maybe_res) mutable {
            if (std::get_if<WrappedResult<void>>(&maybe_res)) {
                try {
                    if constexpr (std::is_void_v<Res1>) {
                        fun();
                        std::move(promise).fulfill();
                    } else {
                        std::move(promise).fulfill(fun());
                    }
                } catch (...) {
                    std::move(promise).fail(std::current_exception());
                }
            } else {
                std::move(promise).fail(std::get<std::exception_ptr>(maybe_res));
            }
        };
        std::move(*this).consume(std::move(consumer));

        return future;
    }

    inline void Future<void>::consume(typename Base::Consumer consumer) && {
        if (!Base::BoundBase::was_bound()) {
            assertion_failed("future was not bound to any promise");
        }

        if (!Base::awaited) {
            Base::awaited = true;
        } else {
            assertion_failed("attempt to await already awaited future");
        }

        if (Base::result.has_value()) {
            consumer(WrappedResult<void>{});
        } else if (Base::error) {
            consumer(Base::error);
        } else {
            Base::BoundBase::get_bound_obj().consumer = std::move(consumer);
        }
    }

    template<FutureResult Res>
    void Promise<Res>::fulfill(Res res) && {
        if (!Base::BoundBase::was_bound()) {
            assertion_failed("promise was not bound to any future");
        }

        if (!Base::fulfilled) {
            Base::fulfilled = true;
        } else {
            assertion_failed("attempt to fulfill already fulfilled promise");
        }

        if (Base::consumer.has_value()) {
            Base::consumer.value()(WrappedResult<Res>{std::move(res)});
        } else {
            Base::BoundBase::get_bound_obj().result.emplace(std::move(res));
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
            Base::consumer.value()(WrappedResult<void>{});
        } else {
            Base::BoundBase::get_bound_obj().result = WrappedResult<void>{};
        }
    }

    template<typename Res, typename Res1>
    Future<bool> operator|(Future<Res> &&future, Future<Res1> &&future1) {
        Future<bool> result;
        auto promise = std::make_shared<Promise<bool>>();
        AIO::bind(result, *promise);

        std::move(future).consume([promise](auto...) {
            if (!promise->is_fulfilled()) {
                std::move(*promise).fulfill(true);
            }
        });

        std::move(future1).consume([promise](auto...) {
            if (!promise->is_fulfilled()) {
                std::move(*promise).fulfill(false);
            }
        });

        return result;
    }

} // namespace AIO
