#pragma once

#include <functional>
#include <type_traits>

#include "util.hpp"

namespace AIO {
class BasicEventLoop;

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

    ~FutureBase();

  protected:
    using Result = Res;
    using ExpectedResult = ExpectedResult<Res>;
    using Consumer = std::move_only_function<void(ExpectedResult)>;

    // TODO: make this private
    template<typename Consumer>
    void consume_with(Consumer &&consumer);

    template<typename AsyncFunctor, typename Res1 = std::invoke_result_t<AsyncFunctor, Res>::Result>
    auto then(AsyncFunctor &&functor) &&;

    template<typename Exception, typename AsyncHandler>
    Future except(AsyncHandler &&handler) &&;

    // TODO: refactor usages and remove this
    template<typename Functor, typename Res1 = std::invoke_result_t<Functor, Res>>
    auto map(Functor &&functor) &&;

    void detach() &&;

  private:
    template<FutureResult Res1, typename Future1, typename Promise1>
    friend class FutureBase;
    template<FutureResult Res1, typename Promise1, typename Future1>
    friend class PromiseBase;

    bool awaited = false;
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

    bool is_fulfilled() const;

    ~PromiseBase();

  protected:
    using Result = Res;
    using ExpectedResult = ExpectedResult<Res>;
    using Consumer = std::move_only_function<void(ExpectedResult)>;

    // TODO: make this private
    void set(ExpectedResult result) &&;

    template<typename Exception>
    void fail(const Exception &e) &&;

    void fulfill(Result res) &&;

  private:
    template<FutureResult Res1, typename Future1, typename Promise1>
    friend class FutureBase;
    template<FutureResult Res1, typename Promise1, typename Future1>
    friend class PromiseBase;

    bool fulfilled = false;
    std::optional<Consumer> maybe_consumer = std::nullopt;
  };

  struct Void {};
} // namespace _impl

template<FutureResult Res>
class Promise;

template<FutureResult Res>
class Future final : public _impl::FutureBase<Res, Future<Res>, Promise<Res>> {
  using FutureBase = _impl::FutureBase<Res, Future, Promise<Res>>;

public:
  template<typename Res1>
  using Mapped = Future<Res1>;
  using Result = Res;

  using FutureBase::FutureBase;

  template<typename AsyncFunctor, typename Res1 = std::invoke_result_t<AsyncFunctor, Res>::Result>
  Future<Res1> then(AsyncFunctor &&functor) &&;

  using FutureBase::except;

  template<typename Functor, typename Res1 = std::invoke_result_t<Functor, Res>>
  Mapped<Res1> map(Functor &&functor) &&;

  using FutureBase::consume_with;

  using FutureBase::detach;
};

template<>
class Future<void> final : public _impl::FutureBase<_impl::Void, Future<_impl::Void>, Promise<_impl::Void>> {
public:
  template<typename Res1>
  using Mapped = Future<Res1>;
  using Result = void;

  using FutureBase::FutureBase;

  template<typename AsyncFunctor, typename Res1 = std::invoke_result_t<AsyncFunctor>::Result>
  Future<Res1> then(AsyncFunctor &&functor) &&;

  template<typename Exception, typename AsyncHandler>
  Future except(AsyncHandler &&handler) &&;

  template<typename Functor, typename Res1 = std::invoke_result_t<Functor>>
  Mapped<Res1> map(Functor &&functor) &&;

  template<typename Consumer>
  void consume_with(Consumer &&consumer);

  using FutureBase::detach;
};

template<FutureResult Res>
class Promise final : public _impl::PromiseBase<Res, Promise<Res>, Future<Res>> {
  using PromiseBase = _impl::PromiseBase<Res, Promise, Future<Res>>;

public:
  template<typename Res1>
  using Mapped = Promise<Res1>;
  using Result = PromiseBase::Result;

  using PromiseBase::PromiseBase;

  using PromiseBase::set;

  using PromiseBase::fail;

  using PromiseBase::fulfill;
};

template<>
class Promise<void> final : public _impl::PromiseBase<_impl::Void, Promise<_impl::Void>, Future<_impl::Void>> {
public:
  template<typename Res1>
  using Mapped = Promise<Res1>;
  using Result = void;

  using PromiseBase::PromiseBase;

  void set(ExpectedResult::Mapped<void> result) &&;

  using PromiseBase::fail;

  void fulfill() &&;
};

template<typename Res, typename Res1>
Future<bool> operator|(Future<Res> &&future, Future<Res1> &&future1);

namespace _impl {
  Future<void> true_void(Future<Void> future);
  Future<Void> fake_void(Future<void> future);

  // TODO: get rid of this
  ExpectedResult<void> true_void(ExpectedResult<Void> result);
  ExpectedResult<Void> fake_void(ExpectedResult<void> result);
} // namespace _impl

} // namespace AIO

#include "aio_bits/future.tcc"
