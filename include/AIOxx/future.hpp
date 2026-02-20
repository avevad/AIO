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

namespace _impl {
  template<FutureResult Res, typename Promise, typename Future>
  class PromiseBase;

  template<FutureResult Res, typename Future, typename Promise>
  class FutureBase : public Bond<FutureBase<Res, Future, Promise>, PromiseBase<Res, Promise, Future>> {
    using Bond = Bond<FutureBase, PromiseBase<Res, Promise, Future>>;

  public:
    FutureBase() = default;
    FutureBase(FutureBase &&other) noexcept;
    FutureBase &operator=(FutureBase &&other) noexcept;

    class AbstractConsumer {
    public:
      virtual void set_cancellation_handle(Future future) = 0;
      virtual void consume(ExpectedResult<Res> result) = 0;
      virtual ~AbstractConsumer() = default;
    };

    FutureBase(const FutureBase &) = delete;
    FutureBase &operator=(const FutureBase &) = delete;

    template<typename P>
    void bind_to(P &promise);

    [[nodiscard]] bool is_free() const;

    ~FutureBase();

  protected:
    using Result = Res;
    using ExpectedResult = ExpectedResult<Res>;
    using HangupHandler = std::move_only_function<void()>;
    using ConsumerPtr = std::unique_ptr<AbstractConsumer>;

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

    void consume_with(ConsumerPtr consumer) &&;

    bool consumed = false;
    std::optional<ExpectedResult> maybe_result = std::nullopt;
    std::optional<HangupHandler> maybe_handler = std::nullopt;
  };

  template<FutureResult Res, typename Promise, typename Future>
  class PromiseBase : public Bond<PromiseBase<Res, Promise, Future>, FutureBase<Res, Future, Promise>> {
    using Bond = Bond<PromiseBase, FutureBase<Res, Future, Promise>>;

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
    using ConsumerBase = typename FutureBase<Res, Future, Promise>::AbstractConsumer;
    using ConsumerPtr = std::unique_ptr<ConsumerBase>;

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
    ConsumerPtr maybe_consumer = nullptr;
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
  FutureBase<Res, Future, Promise>::~FutureBase() {
    AIOXX_ASSUME(is_free());
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename AsyncFunctor, typename Res1>
  auto FutureBase<Res, Future, Promise>::then(AsyncFunctor &&functor) && {
    using MappedFuture = Future::template Mapped<Res1>;
    using MappedExpected = ExpectedResult::template Mapped<Res1>;
    using MappedPromise = AIO::Promise<Res1>;

    struct MappedConsumer final : MappedFuture::AbstractConsumer {
      MappedPromise promise;
      explicit MappedConsumer(MappedPromise promise) : promise(std::move(promise)) {}

      void set_cancellation_handle(MappedFuture future) override {
        promise.set_hangup_handler([future = std::move(future)]() mutable { std::move(future).cancel(); });
      }

      void consume(MappedExpected result) override {
        std::move(promise).set(std::move(result));
      }
    };

    struct Consumer final : AbstractConsumer {
      MappedPromise promise;
      std::decay_t<AsyncFunctor> fun;
      explicit Consumer(MappedPromise promise, std::decay_t<AsyncFunctor> &&fun) : promise(std::move(promise)), fun(std::move(fun)) {}

      void set_cancellation_handle(Future future) override {
        promise.set_hangup_handler([future = std::move(future)]() mutable { std::move(future).cancel(); });
      }

      void consume(ExpectedResult result) override {
        if (result.is_ok()) {
          try {
            MappedFuture future1 = fun(result.move_as_ok());
            std::move(future1).consume_with(std::make_unique<MappedConsumer>(std::move(promise)));
          } catch (...) {
            std::move(promise).set(MappedExpected::make_err_from_current());
          }
        } else {
          std::move(promise).set(MappedExpected::make_err(result.move_as_err()));
        }
      }
    };

    auto [promise, future] = Contract<Res1>();
    std::move(*this).consume_with(std::make_unique<Consumer>(std::move(promise), std::forward<AsyncFunctor>(functor)));
    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Exception, typename AsyncHandler>
  Future FutureBase<Res, Future, Promise>::except(AsyncHandler &&handler) && {
    struct NestedConsumer final : AbstractConsumer {
      Promise promise;
      explicit NestedConsumer(Promise promise) : promise(std::move(promise)) {}

      void set_cancellation_handle(Future future) override {
        promise.set_hangup_handler([future = std::move(future)]() mutable { std::move(future).cancel(); });
      }

      void consume(ExpectedResult result) override {
        std::move(promise).set(std::move(result));
      }
    };

    struct Consumer final : AbstractConsumer {
      Promise promise;
      std::decay_t<AsyncHandler> handler;
      explicit Consumer(Promise promise, std::decay_t<AsyncHandler> &&handler)
          : promise(std::move(promise)), handler(std::move(handler)) {
      }

      void set_cancellation_handle(Future future) override {
        promise.set_hangup_handler([future = std::move(future)]() mutable { std::move(future).cancel(); });
      }

      void consume(ExpectedResult result) override {
        if (!result.is_ok()) {
          try {
            std::rethrow_exception(result.move_as_err());
          } catch (Exception &e) {
            try {
              Future future1 = handler(e);
              std::move(future1).consume_with(std::make_unique<NestedConsumer>(std::move(promise)));
            } catch (...) {
              std::move(promise).set(ExpectedResult::make_err_from_current());
            }
          } catch (...) {
            std::move(promise).set(ExpectedResult::make_err_from_current());
          }
        } else {
          std::move(promise).set(std::move(result));
        }
      }
    };

    auto [promise, future] = Contract<Res>();
    std::move(*this).consume_with(std::make_unique<Consumer>(std::move(promise), std::forward<AsyncHandler>(handler)));
    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename AsyncHandler>
  Future FutureBase<Res, Future, Promise>::except_any(AsyncHandler &&handler) && {
    struct NestedConsumer final : AbstractConsumer {
      Promise promise;
      explicit NestedConsumer(Promise promise) : promise(std::move(promise)) {}

      void set_cancellation_handle(Future future) override {
        promise.set_hangup_handler([future = std::move(future)]() mutable { std::move(future).cancel(); });
      }

      void consume(ExpectedResult result) override {
        std::move(promise).set(std::move(result));
      }
    };

    struct Consumer final : AbstractConsumer {
      Promise promise;
      std::decay_t<AsyncHandler> handler;
      explicit Consumer(Promise promise, std::decay_t<AsyncHandler> &&handler)
          : promise(std::move(promise)), handler(std::move(handler)) {
      }

      void set_cancellation_handle(Future future) override {
        promise.set_hangup_handler([future = std::move(future)]() mutable { std::move(future).cancel(); });
      }

      void consume(ExpectedResult result) override {
        if (!result.is_ok()) {
          try {
            std::rethrow_exception(result.move_as_err());
          } catch (...) {
            try {
              Future future1 = handler(std::current_exception());
              std::move(future1).consume_with(std::make_unique<NestedConsumer>(std::move(promise)));
            } catch (...) {
              std::move(promise).set(ExpectedResult::make_err_from_current());
            }
          }
        } else {
          std::move(promise).set(std::move(result));
        }
      }
    };

    auto [promise, future] = Contract<Res>();
    std::move(*this).consume_with(std::make_unique<Consumer>(std::move(promise), std::forward<AsyncHandler>(handler)));
    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Functor, typename Res1>
  auto FutureBase<Res, Future, Promise>::map_result(Functor &&functor) && {
    using MappedExpected = ExpectedResult::template Mapped<Res1>;
    using MappedPromise = AIO::Promise<Res1>;

    struct Consumer final : AbstractConsumer {
      MappedPromise promise;
      std::decay_t<Functor> functor;
      explicit Consumer(MappedPromise promise, std::decay_t<Functor> &&functor)
          : promise(std::move(promise)), functor(std::move(functor)) {
      }

      void set_cancellation_handle(Future future) override {
        promise.set_hangup_handler([future = std::move(future)]() mutable { std::move(future).cancel(); });
      }

      void consume(ExpectedResult result) override {
        if (result.is_ok()) {
          try {
            std::move(promise).fulfill(functor(result.move_as_ok()));
          } catch (...) {
            std::move(promise).set(MappedExpected::make_err_from_current());
          }
        } else {
          std::move(promise).set(MappedExpected::make_err(result.move_as_err()));
        }
      }
    };

    auto [promise, future] = Contract<Res1>();
    std::move(*this).consume_with(std::make_unique<Consumer>(std::move(promise), std::forward<Functor>(functor)));
    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Functor, typename Res1>
  auto FutureBase<Res, Future, Promise>::map_expected(Functor &&functor) && {
    using MappedExpected = ExpectedResult::template Mapped<Res1>;
    using MappedPromise = AIO::Promise<Res1>;

    struct Consumer final : AbstractConsumer {
      MappedPromise promise;
      std::decay_t<Functor> functor;
      explicit Consumer(MappedPromise promise, std::decay_t<Functor> &&functor)
          : promise(std::move(promise)), functor(std::move(functor)) {
      }

      void set_cancellation_handle(Future future) override {
        promise.set_hangup_handler([future = std::move(future)]() mutable { std::move(future).cancel(); });
      }

      void consume(ExpectedResult result) override {
        try {
          std::move(promise).set(MappedExpected{.expected = functor(std::move(result.expected))});
        } catch (...) {
          std::move(promise).set(MappedExpected::make_err_from_current());
        }
      }
    };

    auto [promise, future] = Contract<Res1>();
    std::move(*this).consume_with(std::make_unique<Consumer>(std::move(promise), std::forward<Functor>(functor)));

    return std::move(future);
  }

  template<FutureResult Res, typename Future, typename Promise>
  void FutureBase<Res, Future, Promise>::cancel() && {
    AIOXX_ASSUME(Bond::is_initialized());
    AIOXX_ASSUME(maybe_handler.has_value());

    if (!consumed) {
      (*maybe_handler)();
      maybe_handler.reset();
    }

    if (Bond::is_alive()) {
      Bond::get().hangup = true;
      Bond::get().maybe_consumer.reset();
    }

    auto _ = std::move(*this);
  }

  template<FutureResult Res, typename Future, typename Promise>
  void FutureBase<Res, Future, Promise>::detach() && {
    struct Consumer final : AbstractConsumer {
      void set_cancellation_handle(Future) override {
      }

      void consume(ExpectedResult result) override {
        if (!result.is_ok()) {
          try {
            std::rethrow_exception(result.move_as_err());
          } catch (const CoroutineKiller &) {
            // TODO: this shouldn't be here. See `BasicEventLoop::fiber()`.
          } catch (...) {
            warning("unhandled error in detached future", std::current_exception());
          }
        }
      }
    };

    std::move(*this).consume_with(std::make_unique<Consumer>());
  }

  template<FutureResult Res, typename Future, typename Promise>
  void FutureBase<Res, Future, Promise>::consume_with(ConsumerPtr consumer) && {
    AIOXX_ASSUME(Bond::is_initialized());
    AIOXX_ASSUME(!consumed);

    if (!maybe_handler.has_value())
      return;

    consumed = true;
    auto maybe_result1 = std::move(maybe_result);
    maybe_result.reset();
    auto *bound_promise = Bond::is_alive() ? &Bond::get() : nullptr;

    if (consumer != nullptr) {
      consumer->set_cancellation_handle(std::move(*static_cast<Future *>(this)));
    }

    if (maybe_result1.has_value()) {
      AIOXX_ASSUME(consumer != nullptr);
      consumer->consume(std::move(*maybe_result1));
    } else if (bound_promise != nullptr) {
      bound_promise->maybe_consumer = std::move(consumer);
    }
  }

  template<FutureResult Res, typename Promise, typename Future>
  PromiseBase<Res, Promise, Future>::PromiseBase(PromiseBase &&other) noexcept
      : Bond(std::move(other)), fulfilled(other.fulfilled), maybe_consumer(std::move(other.maybe_consumer)), hangup(other.hangup) {
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
    if (maybe_consumer != nullptr) {
      maybe_consumer->consume(std::move(result));
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
    return Future<void>(std::move(*this).FutureBase::then([functor =
                                                             std::forward<AsyncFunctor>(functor)](Res res) mutable {
      return Future<_impl::Void>(functor(std::move(res)));
    }));
  } else {
    return std::move(*this).FutureBase::then(std::forward<AsyncFunctor>(functor));
  }
}

template<FutureResult Res>
template<typename Functor, typename Res1>
Future<Res>::Mapped<Res1> Future<Res>::map_result(Functor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return Future<void>(std::move(*this).FutureBase::map_result([functor =
                                                                   std::forward<Functor>(functor)](Res res) mutable {
      functor(std::move(res));
      return _impl::Void{};
    }));
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
