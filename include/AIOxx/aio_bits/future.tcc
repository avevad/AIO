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
    other.maybe_result.reset();

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
        try {
          MappedFuture future1 = fun(result.move_as_ok());
          std::move(future1).consume_with([promise = std::move(promise)](auto result1) mutable {
            std::move(promise).set(std::move(result1));
          });
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
          try {
            Future future1 = handler(e);
            std::move(future1).consume_with([promise = std::move(promise)](ExpectedResult result1) mutable {
              std::move(promise).set(std::move(result1));
            });
          } catch (...) {
            std::move(promise).set(ExpectedResult::make_err_from_current());
          }
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
  template<typename AsyncHandler>
  Future FutureBase<Res, Future, Promise>::except_any(AsyncHandler &&handler) && {
    Future future;
    Promise promise;
    AIO::bind(promise, future);

    std::move(*this).consume_with([promise = std::move(promise),
                                   handler = std::forward<AsyncHandler>(handler)](ExpectedResult result) mutable {
      if (!result.is_ok()) {
        try {
          std::rethrow_exception(result.move_as_err());
        } catch (...) {
          try {
            Future future1 = handler(std::current_exception());
            std::move(future1).consume_with([promise = std::move(promise)](ExpectedResult result1) mutable {
              std::move(promise).set(std::move(result1));
            });
          } catch (...) {
            std::move(promise).set(ExpectedResult::make_err_from_current());
          }
        }
      } else {
        std::move(promise).set(std::move(result));
      }
    });
    return future;
  }

  template<FutureResult Res, typename Future, typename Promise>
  template<typename Functor, typename Res1>
  auto FutureBase<Res, Future, Promise>::map_result(Functor &&functor) && {
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
  template<typename Functor, typename Res1>
  auto FutureBase<Res, Future, Promise>::map_expected(Functor &&functor) && {
    using MappedFuture = Future::template Mapped<Res1>;
    using MappedPromise = Promise::template Mapped<Res1>;
    using MappedExpected = ExpectedResult::template Mapped<Res1>;

    MappedFuture future;
    MappedPromise promise;
    AIO::bind(promise, future);

    std::move(*this).consume_with([promise = std::move(promise),
                                   functor = std::forward<Functor>(functor)](ExpectedResult result) mutable {
      try {
        std::move(promise).set(MappedExpected{.expected = functor(std::move(result.expected))});
      } catch (...) {
        std::move(promise).set(MappedExpected::make_err_from_current());
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
  PromiseBase<Res, Promise, Future>::~PromiseBase() {
    if (!fulfilled) {
      warning("destroying non-fulfilled promise");
    }
  }

  template<FutureResult Res, typename Promise, typename Future>
  template<typename Exception>
  void PromiseBase<Res, Promise, Future>::fail(const Exception &e) && {
    std::move(*this).set(ExpectedResult::make_err_from(e));
  }

  template<FutureResult Res, typename Promise, typename Future>
  void PromiseBase<Res, Promise, Future>::fail_any(std::exception_ptr err) {
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

    fulfilled = true;

    if (maybe_consumer.has_value()) {
      (*maybe_consumer)(std::move(result));
    } else {
      Bond::get().maybe_result.emplace(std::move(result));
    }
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
    return std::move(*this).FutureBase::map_result([functor = std::forward<Functor>(functor)](Expected expected) {
      return functor(std::move(expected).transform([](auto) {}));
    });
  }
}

inline Future<void>::Future(Future<_impl::Void> &&other) noexcept : FutureBase(std::move(other)) {
}

inline void Promise<void>::fulfill() && {
  std::move(*this).PromiseBase::fulfill(_impl::Void{});
}

// TODO: this is ugly and wrong, should be refactored after implementing Future.cancel()
template<typename Res, typename Res1>
Future<bool> operator|(Future<Res> &&future1, Future<Res1> &&future2) {
  Future<bool> future;
  Promise<bool> promise;
  AIO::bind(future, promise);

  struct State {
    Promise<bool> promise;
    bool done;
  };
  std::shared_ptr<State> state = std::make_shared<State>();
  state->promise = std::move(promise);

  std::move(future1)
    .map_expected([state](auto...) mutable {
      if (!state->done) {
        state->done = true;
        std::move(state->promise).fulfill(true);
      }
      return Expected<void>{};
    })
    .detach();

  std::move(future2)
    .map_expected([state](auto...) mutable {
      if (!state->done) {
        state->done = true;
        std::move(state->promise).fulfill(false);
      }
      return Expected<void>{};
    })
    .detach();

  return future;
}
} // namespace AIO
