#pragma once

#include <memory>

namespace AIO {
namespace _impl {
  template<FutureResult Res, typename Future, typename Promise>
  FutureBase<Res, Future, Promise>::FutureBase(FutureBase &&other) noexcept
      : Bond(std::move(other)), awaited(other.awaited), maybe_result(std::move(other.maybe_result)) {
    other.maybe_result.reset();
    other.awaited = true;
  }

  template<FutureResult Res, typename Future, typename Promise>
  FutureBase<Res, Future, Promise> &FutureBase<Res, Future, Promise>::operator=(FutureBase &&other) noexcept {
    Bond::operator=(std::move(other));

    maybe_result = std::move(other.maybe_result);
    other.result.reset();

    awaited = other.awaited;
    other.awaited = true;

    return *this;
  }

  template<FutureResult Res, typename Future, typename Promise>
  FutureBase<Res, Future, Promise>::~FutureBase() {
    AIOXX_ASSUME(awaited);
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename AsyncFunctor, typename Res1>
  auto FutureBase<Res, Future, Promise>::then(AsyncFunctor &&functor) && {
    using MappedFuture = Future::template Mapped<Res1>;
    using MappedPromise = Promise::template Mapped<Res1>;
    using MappedExpected = ExpectedResult::template Mapped<Res1>;

    MappedFuture future;
    MappedPromise promise;
    AIO::bind(promise, future);

    std::move(*this).consume_with([promise = std::move(promise),
                                   fun = std::forward<AsyncFunctor>(functor)](ExpectedResult result) mutable {
      if (result.is_ok()) {
        MappedFuture future1 = fun(result.move_as_ok());
        std::move(future1).consume_with([promise = std::move(promise)](auto result1) mutable {
          std::move(promise).set(std::move(result1));
        });
      } else {
        std::move(promise).set(MappedExpected::make_err(result.move_as_err()));
      }
    });

    return future;
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Exception, typename AsyncHandler>
  Future FutureBase<Res, Future, Promise>::except(AsyncHandler &&handler) && {
    Future future;
    Promise promise;
    AIO::bind(promise, future);

    std::move(*this).consume_with([promise = std::move(promise),
                                   handler = std::forward<AsyncHandler>(handler)](ExpectedResult result) mutable {
      if (!result.is_ok()) {
        try {
          std::rethrow_exception(result.move_as_err());
        } catch (Exception &e) {
          Future future1 = handler(e);
          std::move(future1).consume_with([promise = std::move(promise)](ExpectedResult result1) mutable {
            std::move(promise).set(std::move(result1));
          });
        } catch (...) {
          std::move(promise).set(ExpectedResult::make_err_from_current());
        }
      } else {
        std::move(promise).set(std::move(result));
      }
    });

    return future;
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Functor, typename Res1>
  auto FutureBase<Res, Future, Promise>::map(Functor &&functor) && {
    using MappedFuture = Future::template Mapped<Res1>;
    using MappedPromise = Promise::template Mapped<Res1>;
    using MappedExpected = ExpectedResult::template Mapped<Res1>;

    MappedFuture future;
    MappedPromise promise;
    AIO::bind(promise, future);

    std::move(*this).consume_with([promise = std::move(promise),
                                   functor = std::forward<Functor>(functor)](ExpectedResult result) mutable {
      if (result.is_ok()) {
        try {
          std::move(promise).fulfill(functor(result.move_as_ok()));
        } catch (...) {
          std::move(promise).set(MappedExpected::make_err_from_current());
        }
      } else {
        std::move(promise).set(MappedExpected::make_err(result.move_as_err()));
      }
    });

    return future;
  }

  template<FutureResult Res, typename Future, typename Promise>
  void FutureBase<Res, Future, Promise>::detach() && {
    std::move(*this).consume_with([](ExpectedResult result) {
      if (!result.is_ok()) {
        try {
          std::rethrow_exception(result.move_as_err());
        } catch (std::exception &e) {
          warning("unhandled error in detached future", e);
        } catch (...) {
          warning("unhandled unknown error in detached future");
        }
      }
    });
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Consumer>
  void FutureBase<Res, Future, Promise>::consume_with(Consumer &&consumer) {
    AIOXX_ASSUME(Bond::is_initialized());
    AIOXX_ASSUME(!awaited);

    awaited = true;
    if (maybe_result.has_value()) {
      consumer(std::move(*maybe_result));
    }

    if (Bond::is_alive()) {
      Bond::get().maybe_consumer = std::forward<Consumer>(consumer);
    }
  }

  template<FutureResult Res, typename Promise, typename Future>
  PromiseBase<Res, Promise, Future>::PromiseBase(PromiseBase &&other) noexcept
      : Bond(std::move(other)), fulfilled(other.fulfilled), maybe_consumer(std::move(other.maybe_consumer)) {
    other.maybe_consumer.reset();
    other.fulfilled = true;
  }

  template<FutureResult Res, typename Promise, typename Future>
  PromiseBase<Res, Promise, Future> &PromiseBase<Res, Promise, Future>::operator=(PromiseBase &&other) noexcept {
    Bond::operator=(std::move(other));

    maybe_consumer = std::move(other.maybe_consumer);
    other.maybe_consumer.reset();

    fulfilled = other.fulfilled;
    other.fulfilled = true;

    return *this;
  }

  template<FutureResult Res, typename Promise, typename Future>
  bool PromiseBase<Res, Promise, Future>::is_fulfilled() const {
    return fulfilled;
  }

  template<FutureResult Res, typename Promise, typename Future>
  PromiseBase<Res, Promise, Future>::~PromiseBase() {
    if (!is_fulfilled()) {
      warning("destroying non-fulfilled promise");
    }
  }

  template<FutureResult Res, typename Promise, typename Future>
  template<typename Exception>
  void PromiseBase<Res, Promise, Future>::fail(const Exception &e) && {
    std::move(*this).set(ExpectedResult::make_err_from(e));
  }

  template<FutureResult Res, typename Promise, typename Future>
  void PromiseBase<Res, Promise, Future>::fulfill(Result res) && {
    std::move(*this).set(ExpectedResult::make_ok(std::move(res)));
  }

  template<FutureResult Res, typename Promise, typename Future>
  void PromiseBase<Res, Promise, Future>::set(ExpectedResult result) && {
    AIOXX_ASSUME(Bond::is_initialized());
    AIOXX_ASSUME(!fulfilled);

    fulfilled = true;

    if (maybe_consumer.has_value()) {
      (*maybe_consumer)(std::move(result));
    } else {
      Bond::get().maybe_result.emplace(std::move(result));
    }
  }
} // namespace _impl

template<FutureResult Res>
template<typename AsyncFunctor, typename Res1>
Future<Res1> Future<Res>::then(AsyncFunctor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return _impl::true_void(
      std::move(*this).FutureBase::then([functor = std::forward<AsyncFunctor>(functor)](Res res) mutable {
        return _impl::fake_void(functor(std::move(res)));
      })
    );
  } else {
    return std::move(*this).FutureBase::then(std::forward<AsyncFunctor>(functor));
  }
}

template<FutureResult Res>
template<typename Functor, typename Res1>
Future<Res>::Mapped<Res1> Future<Res>::map(Functor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return _impl::true_void(
      std::move(*this).FutureBase::map([functor = std::forward<Functor>(functor)](Res res) mutable {
        functor(std::move(res));
        return _impl::Void{};
      })
    );
  } else {
    return std::move(*this).FutureBase::map(std::forward<Functor>(functor));
  }
}
template<typename AsyncFunctor, typename Res1>
Future<Res1> Future<void>::then(AsyncFunctor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return _impl::true_void(
      std::move(*this).FutureBase::then([functor = std::forward<AsyncFunctor>(functor)](_impl::Void) mutable {
        return _impl::fake_void(functor());
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
  return _impl::true_void(
    std::move(*this).FutureBase::except<Exception>(
      [handler = std::forward<AsyncHandler>(handler)](Exception &e) mutable { return _impl::fake_void(handler(e)); }
    )
  );
}

template<typename Functor, typename Res1>
Future<void>::Mapped<Res1> Future<void>::map(Functor &&functor) && {
  if constexpr (std::is_void_v<Res1>) {
    return _impl::true_void(
      std::move(*this).FutureBase::map([functor = std::forward<Functor>(functor)](_impl::Void) mutable {
        functor();
        return _impl::Void{};
      })
    );
  } else {
    return std::move(*this).FutureBase::map([functor = std::forward<Functor>(functor)](_impl::Void) mutable {
      return functor();
    });
  }
}

template<typename Consumer>
void Future<void>::consume_with(Consumer &&consumer) {
  std::move(*this).FutureBase::consume_with([consumer =
                                               std::forward<Consumer>(consumer)](ExpectedResult result) mutable {
    consumer(_impl::true_void(std::move(result)));
  });
}

inline void Promise<void>::set(ExpectedResult::Mapped<void> result) && {
  std::move(*this).PromiseBase::set(_impl::fake_void(std::move(result)));
}

inline void Promise<void>::fulfill() && {
  std::move(*this).PromiseBase::fulfill(_impl::Void{});
}

inline Future<void> _impl::true_void(Future<Void> future) {
  Future<void> future1;
  Promise<void> promise1;
  AIO::bind(future1, promise1);
  future.consume_with([promise1 = std::move(promise1)](ExpectedResult<Void> result) mutable {
    std::move(promise1).set(true_void(std::move(result)));
  });
  return future1;
}

inline Future<_impl::Void> _impl::fake_void(Future<void> future) {
  Future<Void> future1;
  Promise<Void> promise1;
  AIO::bind(future1, promise1);
  future.consume_with([promise1 = std::move(promise1)](ExpectedResult<void> result) mutable {
    std::move(promise1).set(fake_void(std::move(result)));
  });
  return future1;
}

inline ExpectedResult<void> _impl::true_void(ExpectedResult<Void> result) {
  return result.is_ok() ? ExpectedResult<void>::make_ok() : ExpectedResult<void>::make_err(result.move_as_err());
}

inline ExpectedResult<_impl::Void> _impl::fake_void(ExpectedResult<void> result) {
  return result.is_ok() ? ExpectedResult<Void>::make_ok({}) : ExpectedResult<Void>::make_err(result.move_as_err());
}

template<typename Res, typename Res1>
Future<bool> operator|(Future<Res> &&future, Future<Res1> &&future1) {
  Future<bool> result;
  auto promise = std::make_shared<Promise<bool>>();
  bind(result, *promise);

  std::move(future).consume_with([promise](auto...) mutable {
    if (!promise->is_fulfilled()) {
      std::move(*promise).fulfill(true);
    }
  });

  std::move(future1).consume_with([promise](auto...) mutable {
    if (!promise->is_fulfilled()) {
      std::move(*promise).fulfill(false);
    }
  });

  return result;
}
} // namespace AIO
