#pragma once

#include <functional>
#include <type_traits>

#include "util.hpp"

namespace AIO {
class BasicScheduler;

template<typename Res>
concept FutureResult = !std::is_reference_v<Res>;

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

    FutureBase(const FutureBase &) = delete;
    FutureBase &operator=(const FutureBase &) = delete;

    template<typename P>
    void bind_to(P &promise);

    [[nodiscard]] bool is_free() const;

    ~FutureBase();

  protected:
    using Result = Res;
    using ExpectedResult = ExpectedResult<Res>;
    using Consumer = std::move_only_function<void(ExpectedResult)>;

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

    void detach() &&;

  private:
    template<FutureResult Res1, typename Future1, typename Promise1>
    friend class FutureBase;
    template<FutureResult Res1, typename Promise1, typename Future1>
    friend class PromiseBase;

    template<typename Consumer>
    void consume_with(Consumer &&consumer) &&;

    std::optional<ExpectedResult> maybe_result = std::nullopt;
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
    using Consumer = std::move_only_function<void(ExpectedResult)>;

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

    std::optional<Consumer> maybe_consumer = std::nullopt;
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

#include "aio_bits/future.tcc"
