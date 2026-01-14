#pragma once

#include <functional>
#include <type_traits>
#include <variant>

#include "util.hpp"

namespace AIO {
class BasicEventLoop;

template<typename Res>
concept FutureResult = !std::is_reference_v<Res>;

template<FutureResult Res>
class Promise;

template<FutureResult Res>
class Future;

template<typename Res>
struct WrappedResult {
  Res obj;

  auto move_out() {
    return std::move(obj);
  }
};

template<>
struct WrappedResult<void> {
  void move_out() {
  }
};

template<typename Res>
using MaybeResult = std::variant<WrappedResult<Res>, std::exception_ptr>;

namespace _impl {
  template<FutureResult Res, typename Derived>
  class PromiseBase;

  template<FutureResult Res, typename Derived>
  class FutureBase : public Bound<Derived, Promise<Res>> {
  public:
    using Result = Res;

    FutureBase() = default;
    FutureBase(FutureBase &&other) noexcept;
    FutureBase &operator=(FutureBase &&other) noexcept;

    void detach() &&;

    template<typename Error, typename ErrorHandler>
    Future<Res> except(ErrorHandler &&handler);

    ~FutureBase();

  private:
    using BoundBase = Bound<Derived, Promise<Res>>;
    using Consumer = std::move_only_function<void(MaybeResult<Res>)>;

    friend Promise<Res>;
    friend PromiseBase<Res, Promise<Res>>;
    friend BasicEventLoop;
    friend Derived;

    bool awaited = false;
    std::optional<WrappedResult<Res>> result = std::nullopt;
    std::exception_ptr error = nullptr;
  };

  template<FutureResult Res, typename Derived>
  class PromiseBase : public Bound<Derived, Future<Res>> {
  public:
    using Result = Res;

    PromiseBase() = default;
    PromiseBase(PromiseBase &&other) noexcept;
    PromiseBase &operator=(PromiseBase &&other) noexcept;

    void fail(std::exception_ptr error) &&;
    void propagate(MaybeResult<Res> maybe_res) &&;

    [[nodiscard]] bool is_fulfilled();

    ~PromiseBase();

  private:
    using BoundBase = Bound<Derived, Future<Res>>;
    using Consumer = typename Future<Res>::Consumer;

    friend Future<Res>;
    friend FutureBase<Res, Future<Res>>;
    friend BasicEventLoop;
    friend Derived;

    bool fulfilled = false;
    std::optional<Consumer> consumer = std::nullopt;
  };

} // namespace _impl

template<FutureResult Res>
class Future final : public _impl::FutureBase<Res, Future<Res>> {
  using Base = _impl::FutureBase<Res, Future>;

public:
  using Base::Base;

  void consume(typename Base::Consumer consumer) &&;

  template<typename AsyncFunctor, typename Res1 = typename std::invoke_result_t<AsyncFunctor, Res>::Result>
  Future<Res1> then(AsyncFunctor &&fun) &&;

  template<typename Functor, typename Res1 = std::invoke_result_t<Functor, Res>>
  Future<Res1> map(Functor &&fun) &&;

private:
  friend Base;
};

template<>
class Future<void> final : public _impl::FutureBase<void, Future<void>> {
  using Base = _impl::FutureBase<void, Future>;

public:
  using Base::Base;

  void consume(typename Base::Consumer consumer) &&;

  template<typename AsyncFunctor, typename Res1 = typename std::invoke_result_t<AsyncFunctor>::Result>
  Future<Res1> then(AsyncFunctor &&fun) &&;

  template<typename Functor, typename Res1 = std::invoke_result_t<Functor>>
  Future<Res1> map(Functor &&fun) &&;

private:
  friend Base;
};

template<FutureResult Res>
class Promise final : public _impl::PromiseBase<Res, Promise<Res>> {
  using Base = _impl::PromiseBase<Res, Promise>;

public:
  using Base::Base;

  void fulfill(Res res) &&;

private:
  friend Base;
};

template<>
class Promise<void> final : public _impl::PromiseBase<void, Promise<void>> {
  using Base = _impl::PromiseBase<void, Promise>;

public:
  using Base::Base;

  void fulfill() &&;

private:
  friend Base;
};

template<typename Res, typename Res1>
Future<bool> operator|(Future<Res> &&future, Future<Res1> &&future1);

} // namespace AIO

#include "aio_bits/future.tcc"
