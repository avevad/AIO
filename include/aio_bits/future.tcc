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
        if ((BoundBase::get_bound_ptr() && !BoundBase::get_bound_obj().fulfilled) || result.has_value()) {
            assertion_failed("destroying non-awaited future");
        }
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
        if (BoundBase::get_bound_ptr() && !fulfilled) {
            assertion_failed("destroying non-fulfilled promise");
        }
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
        Base::consumer.emplace(fun);
        if (Base::result.has_value()) {
            Base::consumer.value()(std::move(Base::result.value()));
            Base::result.reset();
        }
    }
    void Future<void>::set_consumer_impl(auto &&fun) {
        if (Base::consumer.has_value()) {
            assertion_failed("attempt to reset consumer");
        }
        Base::consumer.emplace(fun);
        if (Base::result.has_value()) {
            Base::consumer.value()();
            Base::result.reset();
        }
    }

    template<FutureResult Res>
    template<typename FulfillRes>
    void Promise<Res>::fulfill_impl(FulfillRes &&res) {
        if (Base::fulfilled) {
            assertion_failed("attempt to fulfill already fulfilled promise");
        }
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
        if (Base::fulfilled) {
            assertion_failed("attempt to fulfill already fulfilled promise");
        }
        if (auto *future_ptr = Base::BoundBase::get_bound_ptr()) {
            future_ptr->accept();
        }
        Base::fulfilled = true;
    }
} // namespace AIO
