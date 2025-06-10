#pragma once

#include <type_traits>
#include <variant>
#include <functional>

#include "util.hpp"

namespace AIO {
    class SimpleEventLoop;

    template<typename Res>
    concept FutureResult = !std::is_reference_v<Res>;

    template<FutureResult Res>
    class Promise;

    template<FutureResult Res>
    class Future;

    namespace _impl {
        template<FutureResult Res>
        struct MetaConsumerSignature {
            using Type = void(Res);
        };

        template<>
        struct MetaConsumerSignature<void> {
            using Type = void();
        };

        template<FutureResult Res>
        using MetaConsumerSignatureT = typename MetaConsumerSignature<Res>::Type;

        template<FutureResult Res>
        using MetaFutureResultSubstituteT = std::conditional_t<std::is_void_v<Res>, std::monostate, Res>;

        template<FutureResult Res, typename Derived>
        class PromiseBase;

        template<FutureResult Res, typename Derived>
        class FutureBase : public Bound<Derived, Promise<Res>> {
        public:
            using Result = Res;

            FutureBase() = default;
            FutureBase(FutureBase &&other) noexcept;
            FutureBase &operator=(FutureBase &&other) noexcept;

            ~FutureBase();

        private:
            using BoundBase = Bound<Derived, Promise<Res>>;
            using Consumer = std::move_only_function<MetaConsumerSignatureT<Res>>;
            using ResultSubstitute = MetaFutureResultSubstituteT<Res>;

            friend Promise<Res>;
            friend PromiseBase<Res, Promise<Res>>;
            friend SimpleEventLoop;
            friend Derived;

            bool awaited = false;
            std::optional<ResultSubstitute> result = std::nullopt;
        };

        template<FutureResult Res, typename Derived>
        class PromiseBase : public Bound<Derived, Future<Res>> {
        public:
            using Result = Res;

            PromiseBase() = default;
            PromiseBase(PromiseBase &&other) noexcept;
            PromiseBase &operator=(PromiseBase &&other) noexcept;

            ~PromiseBase();

        private:
            using BoundBase = Bound<Derived, Future<Res>>;
            using Consumer = typename Future<Res>::Consumer;
            using ResultSubstitute = typename Future<Res>::ResultSubstitute;

            friend Future<Res>;
            friend FutureBase<Res, Future<Res>>;
            friend SimpleEventLoop;
            friend Derived;

            bool fulfilled = false;
            std::optional<Consumer> consumer = std::nullopt;
        };

    } // namespace _impl

    template<FutureResult Res>
    class Future : public _impl::FutureBase<Res, Future<Res>> {
        using Base = _impl::FutureBase<Res, Future>;

    public:
        using Base::Base;

        template<typename AsyncFunctor, typename Res1 = typename std::invoke_result_t<AsyncFunctor, Res>::Result>
        Future<Res1> then(AsyncFunctor &&fun) &&;

    private:
        friend Base;
    };

    template<>
    class Future<void> : public _impl::FutureBase<void, Future<void>> {
        using Base = _impl::FutureBase<void, Future>;

    public:
        using Base::Base;

        template<typename AsyncFunctor, typename Res1 = typename std::invoke_result_t<AsyncFunctor>::Result>
        Future<Res1> then(AsyncFunctor &&fun) &&;

    private:
        friend Base;
    };

    template<FutureResult Res>
    class Promise : public _impl::PromiseBase<Res, Promise<Res>> {
        using Base = _impl::PromiseBase<Res, Promise>;

    public:
        using Base::Base;

    private:
        friend Base;
    };

    template<>
    class Promise<void> : public _impl::PromiseBase<void, Promise<void>> {
        using Base = _impl::PromiseBase<void, Promise>;

    public:
        using Base::Base;

    private:
        friend Base;
    };

    template<typename Res, typename Res1>
    Future<bool> operator|(Future<Res> &&future, Future<Res1> &&future1);

}

#include "aio_bits/future.tcc"