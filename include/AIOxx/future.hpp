#pragma once

#include <functional>
#include <memory>
#include <type_traits>

#include "coroutine.hpp"
#include "util.hpp"

namespace AIO {
class BasicScheduler;

template<typename Res>
concept FutureResult = !std::is_reference_v<Res>;

template<FutureResult Res>
class Future;

template<FutureResult Res>
class ConsumedFuture;

template<FutureResult Res>
class DetachedFuture;

namespace _impl {
  template<FutureResult Res, typename Promise, typename Future>
  class PromiseBase;

  template<FutureResult Res, typename Future, typename Promise>
  class FutureBase : public Bond<FutureBase<Res, Future, Promise>, PromiseBase<Res, Promise, Future>, false> {
    using Bond = Bond<FutureBase, PromiseBase<Res, Promise, Future>, false>;

  public:

    FutureBase() = default;
    FutureBase(FutureBase &&other) noexcept;
    FutureBase &operator=(FutureBase &&other) noexcept;

    FutureBase(const FutureBase &) = delete;
    FutureBase &operator=(const FutureBase &) = delete;

    template<typename P>
    void bind_to(P &promise);

    [[nodiscard]] bool is_free() const;
    [[nodiscard]] bool is_fulfilled() const;
    [[nodiscard]] bool is_completed() const;

    ~FutureBase();

  protected:
    using Result = Res;
    using ExpectedResult = ExpectedResult<Res>;
    using HangupHandler = std::move_only_function<void()>;
    using Consumer = std::function<void(ExpectedResult)>;

    template<typename AsyncFunctor, typename Res1 = std::invoke_result_t<AsyncFunctor, Res>::Result>
    auto then(AsyncFunctor &&functor) &&;

    template<typename Exception, typename AsyncHandler>
    Future except(AsyncHandler &&handler) &&;

    template<typename AsyncHandler>
    Future except_any(AsyncHandler &&handler) &&;

    template<typename Functor, typename Res1 = std::invoke_result_t<Functor, Res>>
    auto map_result(Functor &&functor) &&;

    template<typename Functor, typename Res1 = std::invoke_result_t<Functor, Expected<Res>>::value_type>
    auto map_expected(Functor &&functor) &&;

    void cancel() &&;

    void detach() &&;

  private:
    template<FutureResult Res1, typename Future1, typename Promise1>
    friend class FutureBase;
    template<FutureResult Res1, typename Promise1, typename Future1>
    friend class PromiseBase;

    template<typename Consumer>
    ConsumedFuture<Res> consume_with(Consumer consumer) &&;

    bool consumed = false;
    std::optional<ExpectedResult> maybe_result = std::nullopt;
    std::optional<HangupHandler> maybe_handler = std::nullopt;
  };

  template<FutureResult Res, typename Promise, typename Future>
  class PromiseBase : public Bond<PromiseBase<Res, Promise, Future>, FutureBase<Res, Future, Promise>, true> {
    using Bond = Bond<PromiseBase, FutureBase<Res, Future, Promise>, true>;

  public:
    PromiseBase() = default;
    PromiseBase(PromiseBase &&other) noexcept;
    PromiseBase &operator=(PromiseBase &&other) noexcept;

    PromiseBase(const PromiseBase &) = delete;
    PromiseBase &operator=(const PromiseBase &) = delete;

    template<typename F>
    void bind_to(F &future);

    [[nodiscard]] bool is_free() const;

    ~PromiseBase();

  protected:
    using Result = Res;
    using ExpectedResult = ExpectedResult<Res>;
    using HangupHandler = std::move_only_function<void()>;
    using Consumer = FutureBase<Res, Future, Promise>::Consumer;

    template<typename F1>
    void set_hangup_handler(F1 &&handler);

    template<typename Exception>
    void fail(const Exception &e) &&;

    void fail_any(std::exception_ptr err) &&;

    void fulfill(Result res) &&;

  private:
    template<FutureResult Res1, typename Future1, typename Promise1>
    friend class FutureBase;
    template<FutureResult Res1, typename Promise1, typename Future1>
    friend class PromiseBase;

    void set(ExpectedResult result) &&;

    bool fulfilled = false;
    std::optional<Consumer> maybe_consumer = std::nullopt;
    bool hangup = false;
  };

  struct Void {};
} // namespace _impl

template<FutureResult Res>
class Promise;

template<FutureResult Res>
class [[nodiscard]] Future final : public _impl::FutureBase<Res, Future<Res>, Promise<Res>> {
  using FutureBase = _impl::FutureBase<Res, Future, Promise<Res>>;

public:
  template<typename Res1>
  using Mapped = Future<Res1>;
  using Result = Res;

  using FutureBase::FutureBase;

  explicit Future(Future<void> &&other) noexcept;

  template<typename AsyncFunctor, typename Res1 = std::invoke_result_t<AsyncFunctor, Res>::Result>
  Future<Res1> then(AsyncFunctor &&functor) &&;

  using FutureBase::except;

  using FutureBase::except_any;

  template<typename Functor, typename Res1 = std::invoke_result_t<Functor, Res>>
  Mapped<Res1> map_result(Functor &&functor) &&;

  template<typename Functor, typename Res1 = std::invoke_result_t<Functor, Expected<Res>>::value_type>
  Mapped<Res1> map_expected(Functor &&functor) &&;

  using FutureBase::cancel;

  using FutureBase::detach;
};

template<>
class [[nodiscard]] Future<void> final
    : public _impl::FutureBase<_impl::Void, Future<_impl::Void>, Promise<_impl::Void>> {
public:
  template<typename Res1>
  using Mapped = Future<Res1>;
  using Result = void;

  using FutureBase::FutureBase;

  explicit Future(Future<_impl::Void> &&other) noexcept;

  template<typename AsyncFunctor, typename Res1 = std::invoke_result_t<AsyncFunctor>::Result>
  Future<Res1> then(AsyncFunctor &&functor) &&;

  template<typename Exception, typename AsyncHandler>
  Future except(AsyncHandler &&handler) &&;

  template<typename AsyncHandler>
  Future except_any(AsyncHandler &&handler) &&;

  template<typename Functor, typename Res1 = std::invoke_result_t<Functor>>
  Mapped<Res1> map_result(Functor &&functor) &&;

  template<typename Functor, typename Res1 = std::invoke_result_t<Functor, Expected<void>>::value_type>
  Mapped<Res1> map_expected(Functor &&functor) &&;

  using FutureBase::cancel;

  using FutureBase::detach;
};

template<FutureResult Res>
class [[nodiscard]] Promise final : public _impl::PromiseBase<Res, Promise<Res>, Future<Res>> {
  using PromiseBase = _impl::PromiseBase<Res, Promise, Future<Res>>;

public:
  template<typename Res1>
  using Mapped = Promise<Res1>;
  using Result = PromiseBase::Result;

  using PromiseBase::PromiseBase;

  using PromiseBase::fail;

  using PromiseBase::fail_any;

  using PromiseBase::fulfill;

  using PromiseBase::set_hangup_handler;
};

template<>
class [[nodiscard]] Promise<void> final
    : public _impl::PromiseBase<_impl::Void, Promise<_impl::Void>, Future<_impl::Void>> {
public:
  template<typename Res1>
  using Mapped = Promise<Res1>;
  using Result = void;

  using PromiseBase::PromiseBase;

  using PromiseBase::fail;

  using PromiseBase::fail_any;

  using PromiseBase::set_hangup_handler;

  void fulfill() &&;
};

template<typename Res>
class Contract {
public:
  Contract();

  Promise<Res> promise = {};
  Future<Res> future = {};
};

template<FutureResult Res>
class DetachedFuture {
public:
  explicit DetachedFuture(Future<Res> &&future) noexcept : future(std::move(future)) {
  }

  DetachedFuture(DetachedFuture &&) noexcept = default;
  DetachedFuture &operator=(DetachedFuture &&) noexcept = default;

  DetachedFuture(const DetachedFuture &) = delete;
  DetachedFuture &operator=(const DetachedFuture &) = delete;

  void cancel() &&;
  ConsumedFuture<Res> hold() &&;

private:
  Future<Res> future;
};

template<FutureResult Res>
class [[nodiscard]] ConsumedFuture {
public:
  explicit ConsumedFuture(Future<Res> &&future) noexcept : future(std::move(future)) {
  }

  ConsumedFuture(ConsumedFuture &&) noexcept = default;
  ConsumedFuture &operator=(ConsumedFuture &&) noexcept = default;

  ConsumedFuture(const ConsumedFuture &) = delete;
  ConsumedFuture &operator=(const ConsumedFuture &) = delete;

  ~ConsumedFuture();

  void cancel() &&;
  DetachedFuture<Res> release() &&;

private:
  Future<Res> future;
};
} // namespace AIO


// --------------------------------------------------
// -------------- TEMPLATE DEFINITIONS --------------
// --------------------------------------------------


namespace AIO {
namespace _impl {
  template<FutureResult Res, typename Future, typename Promise>
  FutureBase<Res, Future, Promise>::FutureBase(FutureBase &&other) noexcept
      : Bond(std::move(other)), consumed(other.consumed), maybe_result(std::move(other.maybe_result)),
        maybe_handler(std::move(other.maybe_handler)) {
    other.consumed = false;
    other.maybe_result.reset();
    other.maybe_handler.reset();
  }

  template<FutureResult Res, typename Future, typename Promise>
  FutureBase<Res, Future, Promise> &FutureBase<Res, Future, Promise>::operator=(FutureBase &&other) noexcept {
    AIOXX_ASSUME(is_free());

    Bond::operator=(std::move(other));
    consumed = other.consumed;
    maybe_result = std::move(other.maybe_result);
    maybe_handler = std::move(other.maybe_handler);

    other.consumed = false;
    other.maybe_result.reset();
    other.maybe_handler.reset();

    return *this;
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename P>
  void FutureBase<Res, Future, Promise>::bind_to(P &promise) {
    Bond::initialize(promise);
    promise.set_hangup_handler([] {});
  }
  template<FutureResult Res, typename Future, typename Promise>
  bool FutureBase<Res, Future, Promise>::is_free() const {
    if (!Bond::is_initialized())
      return true;

    if (!maybe_handler.has_value())
      return true;

    return consumed;
  }

  template<FutureResult Res, typename Future, typename Promise>
  bool FutureBase<Res, Future, Promise>::is_fulfilled() const {
    return !Bond::is_alive() || Bond::get().fulfilled;
  }

  template<FutureResult Res, typename Future, typename Promise>
  bool FutureBase<Res, Future, Promise>::is_completed() const {
    return is_fulfilled() && consumed;
  }

  template<FutureResult Res, typename Future, typename Promise>
  FutureBase<Res, Future, Promise>::~FutureBase() {
    AIOXX_ASSUME(is_free());
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename AsyncFunctor, typename Res1>
  auto FutureBase<Res, Future, Promise>::then(AsyncFunctor &&functor) && {
    using MappedFuture = Future::template Mapped<Res1>;
    using MappedExpected = ExpectedResult::template Mapped<Res1>;
    using MappedPromise = AIO::Promise<Res1>;
    auto [promise, future] = Contract<Res1>();
    auto promise_ptr = std::make_shared<MappedPromise>(std::move(promise));
    auto fun_ptr = std::make_shared<std::decay_t<AsyncFunctor>>(std::forward<AsyncFunctor>(functor));

    auto self = std::move(*this).consume_with([promise_ptr, fun_ptr](ExpectedResult result) mutable {
      if (result.is_ok()) {
        try {
          MappedFuture future1 = (*fun_ptr)(result.move_as_ok());
          auto nested_consumed = std::move(future1).consume_with([promise_ptr](MappedExpected result1) mutable {
            std::move(*promise_ptr).set(std::move(result1));
          });
          auto nested_detached = std::move(nested_consumed).release();
          if (!promise_ptr->is_free()) {
            promise_ptr->set_hangup_handler(
              [future = std::move(nested_detached)]() mutable { std::move(future).cancel(); }
            );
          }
        } catch (...) {
          std::move(*promise_ptr).set(MappedExpected::make_err_from_current());
        }
      } else {
        std::move(*promise_ptr).set(MappedExpected::make_err(result.move_as_err()));
      }
    });

    using MappedFutureBase = FutureBase<Res1, MappedFuture, MappedPromise>;
    if (auto *bound_promise = static_cast<MappedFutureBase &>(future).get_bound_promise_ptr()) {
      bound_promise->set_hangup_handler(
        [future = std::move(self).release()]() mutable { std::move(future).cancel(); }
      );
    }

    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Exception, typename AsyncHandler>
  Future FutureBase<Res, Future, Promise>::except(AsyncHandler &&handler) && {
    auto [promise, future] = Contract<Res>();
    auto promise_ptr = std::make_shared<Promise>(std::move(promise));
    auto handler_ptr = std::make_shared<std::decay_t<AsyncHandler>>(std::forward<AsyncHandler>(handler));

    auto self = std::move(*this).consume_with([promise_ptr, handler_ptr](ExpectedResult result) mutable {
      if (!result.is_ok()) {
        try {
          std::rethrow_exception(result.move_as_err());
        } catch (Exception &e) {
          try {
            Future future1 = (*handler_ptr)(e);
            auto nested_consumed = std::move(future1).consume_with([promise_ptr](ExpectedResult result1) mutable {
              std::move(*promise_ptr).set(std::move(result1));
            });
            auto nested_detached = std::move(nested_consumed).release();
            if (!promise_ptr->is_free()) {
              promise_ptr->set_hangup_handler(
                [future = std::move(nested_detached)]() mutable { std::move(future).cancel(); }
              );
            }
          } catch (...) {
            std::move(*promise_ptr).set(ExpectedResult::make_err_from_current());
          }
        } catch (...) {
          std::move(*promise_ptr).set(ExpectedResult::make_err_from_current());
        }
      } else {
        std::move(*promise_ptr).set(std::move(result));
      }
    });

    if (auto *bound_promise = static_cast<FutureBase &>(future).get_bound_promise_ptr()) {
      bound_promise->set_hangup_handler(
        [future = std::move(self).release()]() mutable { std::move(future).cancel(); }
      );
    }

    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename AsyncHandler>
  Future FutureBase<Res, Future, Promise>::except_any(AsyncHandler &&handler) && {
    auto [promise, future] = Contract<Res>();
    auto promise_ptr = std::make_shared<Promise>(std::move(promise));
    auto handler_ptr = std::make_shared<std::decay_t<AsyncHandler>>(std::forward<AsyncHandler>(handler));

    auto self = std::move(*this).consume_with([promise_ptr, handler_ptr](ExpectedResult result) mutable {
      if (!result.is_ok()) {
        try {
          std::rethrow_exception(result.move_as_err());
        } catch (...) {
          try {
            Future future1 = (*handler_ptr)(std::current_exception());
            auto nested_consumed = std::move(future1).consume_with([promise_ptr](ExpectedResult result1) mutable {
              std::move(*promise_ptr).set(std::move(result1));
            });
            auto nested_detached = std::move(nested_consumed).release();
            if (!promise_ptr->is_free()) {
              promise_ptr->set_hangup_handler(
                [future = std::move(nested_detached)]() mutable { std::move(future).cancel(); }
              );
            }
          } catch (...) {
            std::move(*promise_ptr).set(ExpectedResult::make_err_from_current());
          }
        }
      } else {
        std::move(*promise_ptr).set(std::move(result));
      }
    });

    if (auto *bound_promise = static_cast<FutureBase &>(future).get_bound_promise_ptr()) {
      bound_promise->set_hangup_handler(
        [future = std::move(self).release()]() mutable { std::move(future).cancel(); }
      );
    }

    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Functor, typename Res1>
  auto FutureBase<Res, Future, Promise>::map_result(Functor &&functor) && {
    using MappedExpected = ExpectedResult::template Mapped<Res1>;
    using MappedPromise = AIO::Promise<Res1>;
    auto [promise, future] = Contract<Res1>();
    auto promise_ptr = std::make_shared<MappedPromise>(std::move(promise));
    auto functor_ptr = std::make_shared<std::decay_t<Functor>>(std::forward<Functor>(functor));

    auto self = std::move(*this).consume_with([promise_ptr, functor_ptr](ExpectedResult result) mutable {
      if (result.is_ok()) {
        try {
          std::move(*promise_ptr).fulfill((*functor_ptr)(result.move_as_ok()));
        } catch (...) {
          std::move(*promise_ptr).set(MappedExpected::make_err_from_current());
        }
      } else {
        std::move(*promise_ptr).set(MappedExpected::make_err(result.move_as_err()));
      }
    });

    if (auto *bound_promise = future.promise_ptr())
      bound_promise->set_hangup_handler([future = std::move(self).release()]() mutable { std::move(future).cancel(); });

    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Functor, typename Res1>
  auto FutureBase<Res, Future, Promise>::map_expected(Functor &&functor) && {
    using MappedExpected = ExpectedResult::template Mapped<Res1>;
    using MappedPromise = AIO::Promise<Res1>;
    auto [promise, future] = Contract<Res1>();
    auto promise_ptr = std::make_shared<MappedPromise>(std::move(promise));
    auto functor_ptr = std::make_shared<std::decay_t<Functor>>(std::forward<Functor>(functor));

    auto self = std::move(*this).consume_with([promise_ptr, functor_ptr](ExpectedResult result) mutable {
      try {
        std::move(*promise_ptr).set(MappedExpected{.expected = (*functor_ptr)(std::move(result.expected))});
      } catch (...) {
        std::move(*promise_ptr).set(MappedExpected::make_err_from_current());
      }
    });

    if (auto *bound_promise = future.promise_ptr())
      bound_promise->set_hangup_handler([future = std::move(self).release()]() mutable { std::move(future).cancel(); });

    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  void FutureBase<Res, Future, Promise>::cancel() && {
    if (!Bond::is_initialized() || is_completed() || !maybe_handler.has_value())
      return;

    if (Bond::is_alive()) {
      Bond::get().hangup = true;
      Bond::get().maybe_consumer.reset();
    }

    (*maybe_handler)();

    maybe_handler.reset();
    maybe_result.reset();

    auto _ = std::move(*this);
  }

  template<FutureResult Res, typename Future, typename Promise>
  void FutureBase<Res, Future, Promise>::detach() && {
    auto detached =
      std::move(*this)
        .consume_with([](ExpectedResult result) {
          if (!result.is_ok()) {
            try {
              std::rethrow_exception(result.move_as_err());
            } catch (...) {
              warning("unhandled error in detached future", std::current_exception());
            }
          }
        })
        .release();
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Consumer1>
  ConsumedFuture<Res> FutureBase<Res, Future, Promise>::consume_with(Consumer1 consumer) && {
    AIOXX_ASSUME(Bond::is_initialized());
    AIOXX_ASSUME(!consumed);

    if (!maybe_handler.has_value())
      return;

    consumed = true;
    if (maybe_result.has_value()) {
      consumer(std::move(*maybe_result));
      maybe_result.reset();
    } else if (Bond::is_alive())
      Bond::get().maybe_consumer = std::forward<Consumer1>(consumer);

    return ConsumedFuture{std::move(this)};
  }

  template<FutureResult Res, typename Promise, typename Future>
  PromiseBase<Res, Promise, Future>::PromiseBase(PromiseBase &&other) noexcept
      : Bond(std::move(other)), fulfilled(other.fulfilled), maybe_consumer(std::move(other.maybe_consumer)),
        hangup(other.hangup) {
    other.fulfilled = false;
    other.maybe_consumer.reset();
    other.hangup = false;
  }

  template<FutureResult Res, typename Promise, typename Future>
  PromiseBase<Res, Promise, Future> &PromiseBase<Res, Promise, Future>::operator=(PromiseBase &&other) noexcept {
    AIOXX_ASSUME(is_free());

    Bond::operator=(std::move(other));
    fulfilled = other.fulfilled;
    maybe_consumer = std::move(other.maybe_consumer);
    hangup = other.hangup;

    other.fulfilled = false;
    other.maybe_consumer.reset();
    other.hangup = false;

    return *this;
  }

  template<FutureResult Res, typename Promise, typename Future>
  template<typename F>
  void PromiseBase<Res, Promise, Future>::bind_to(F &future) {
    Bond::initialize(future);
    set_hangup_handler([] {});
  }

  template<FutureResult Res, typename Promise, typename Future>
  bool PromiseBase<Res, Promise, Future>::is_free() const {
    if (!Bond::is_initialized())
      return true;

    if (hangup)
      return true;

    return fulfilled;
  }

  template<FutureResult Res, typename Promise, typename Future>
  PromiseBase<Res, Promise, Future>::~PromiseBase() {
    AIOXX_ASSUME(is_free());
  }

  template<FutureResult Res, typename Promise, typename Future>
  template<typename F1>
  void PromiseBase<Res, Promise, Future>::set_hangup_handler(F1 &&handler) {
    AIOXX_ASSUME(Bond::is_initialized());

    if (hangup) {
      handler();
    } else if (Bond::is_alive()) {
      Bond::get().maybe_handler = std::forward<F1>(handler);
    }
  }

  template<FutureResult Res, typename Promise, typename Future>
  template<typename Exception>
  void PromiseBase<Res, Promise, Future>::fail(const Exception &e) && {
    std::move(*this).set(ExpectedResult::make_err_from(e));
  }

  template<FutureResult Res, typename Promise, typename Future>
  void PromiseBase<Res, Promise, Future>::fail_any(std::exception_ptr err) && {
    std::move(*this).set(ExpectedResult::make_err(std::move(err)));
  }

  template<FutureResult Res, typename Promise, typename Future>
  void PromiseBase<Res, Promise, Future>::fulfill(Result res) && {
    std::move(*this).set(ExpectedResult::make_ok(std::move(res)));
  }

  template<FutureResult Res, typename Promise, typename Future>
  void PromiseBase<Res, Promise, Future>::set(ExpectedResult result) && {
    AIOXX_ASSUME(Bond::is_initialized());
    AIOXX_ASSUME(!fulfilled);

    if (hangup)
      return;

    fulfilled = true;
    if (maybe_consumer.has_value()) {
      (*maybe_consumer)(std::move(result));
      maybe_consumer.reset();
    } else if (Bond::is_alive())
      Bond::get().maybe_result.emplace(std::move(result));

    auto _ = std::move(*this);
  }
} // namespace _impl

template<FutureResult Res>
Future<Res>::Future(Future<void> &&other) noexcept : FutureBase(std::move(other)) {
}

template<FutureResult Res>
template<typename AsyncFunctor, typename Res1>
Future<Res1> Future<Res>::then(AsyncFunctor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return Future<void>(
      std::move(*this).FutureBase::then([functor = std::forward<AsyncFunctor>(functor)](Res res) mutable {
        return Future<_impl::Void>(functor(std::move(res)));
      })
    );
  } else {
    return std::move(*this).FutureBase::then(std::forward<AsyncFunctor>(functor));
  }
}

template<FutureResult Res>
template<typename Functor, typename Res1>
Future<Res>::Mapped<Res1> Future<Res>::map_result(Functor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return Future<void>(
      std::move(*this).FutureBase::map_result([functor = std::forward<Functor>(functor)](Res res) mutable {
        functor(std::move(res));
        return _impl::Void{};
      })
    );
  } else {
    return std::move(*this).FutureBase::map_result(std::forward<Functor>(functor));
  }
}

template<FutureResult Res>
template<typename Functor, typename Res1>
Future<Res>::Mapped<Res1> Future<Res>::map_expected(Functor &&functor) && {
  using Expected = std::expected<Res, std::exception_ptr>;
  if constexpr (std::is_void_v<Res1>) {
    return Future<void>(
      std::move(*this).FutureBase::map_expected([functor = std::forward<Functor>(functor)](Expected expected) mutable {
        return functor(std::move(expected)).transform([] { return _impl::Void{}; });
      })
    );
  } else {
    return std::move(*this).FutureBase::map_expected(std::forward<Functor>(functor));
  }
}

template<typename AsyncFunctor, typename Res1>
Future<Res1> Future<void>::then(AsyncFunctor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return Future(
      std::move(*this).FutureBase::then([functor = std::forward<AsyncFunctor>(functor)](_impl::Void) mutable {
        return Future<_impl::Void>(functor());
      })
    );
  } else {
    return std::move(*this).FutureBase::then([functor = std::forward<AsyncFunctor>(functor)](_impl::Void) mutable {
      return functor();
    });
  }
}

template<typename Exception, typename AsyncHandler>
Future<void> Future<void>::except(AsyncHandler &&handler) && {
  return Future(
    std::move(*this).FutureBase::except<Exception>(
      [handler = std::forward<AsyncHandler>(handler)](Exception &e) mutable { return Future<_impl::Void>(handler(e)); }
    )
  );
}

template<typename AsyncHandler>
Future<void> Future<void>::except_any(AsyncHandler &&handler) && {
  return Future(
    std::move(*this).FutureBase::except_any([handler =
                                               std::forward<AsyncHandler>(handler)](std::exception_ptr err) mutable {
      return Future<_impl::Void>(handler(err));
    })
  );
}

template<typename Functor, typename Res1>
Future<void>::Mapped<Res1> Future<void>::map_result(Functor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return Future(
      std::move(*this).FutureBase::map_result([functor = std::forward<Functor>(functor)](_impl::Void) mutable {
        functor();
        return _impl::Void{};
      })
    );
  } else {
    return std::move(*this).FutureBase::map_result([functor = std::forward<Functor>(functor)](_impl::Void) mutable {
      return functor();
    });
  }
}

template<typename Functor, typename Res1>
Future<void>::Mapped<Res1> Future<void>::map_expected(Functor &&functor) && {
  using Expected = std::expected<_impl::Void, std::exception_ptr>;
  if constexpr (std::is_void_v<Res1>) {
    return Future(
      std::move(*this).FutureBase::map_expected([functor = std::forward<Functor>(functor)](Expected expected) mutable {
        return functor(std::move(expected).transform([](auto) {})).transform([] { return _impl::Void{}; });
      })
    );
  } else {
    return std::move(*this).FutureBase::map_expected([functor = std::forward<Functor>(functor)](Expected expected) {
      return functor(std::move(expected).transform([](auto) {}));
    });
  }
}

template<FutureResult Res>
void DetachedFuture<Res>::cancel() && {
  std::move(future).cancel();
}

template<FutureResult Res>
ConsumedFuture<Res> DetachedFuture<Res>::hold() && {
  return ConsumedFuture<Res>(std::move(future));
}

template<FutureResult Res>
ConsumedFuture<Res>::~ConsumedFuture() {
  std::move(future).cancel();
}

template<FutureResult Res>
void ConsumedFuture<Res>::cancel() && {
  std::move(future).cancel();
}

template<FutureResult Res>
DetachedFuture<Res> ConsumedFuture<Res>::release() && {
  return DetachedFuture<Res>(std::move(future));
}

template<typename Res>
Contract<Res>::Contract() {
  promise.bind_to(future);
}

inline Future<void>::Future(Future<_impl::Void> &&other) noexcept : FutureBase(std::move(other)) {
}

inline void Promise<void>::fulfill() && {
  std::move(*this).PromiseBase::fulfill(_impl::Void{});
}
} // namespace AIO
