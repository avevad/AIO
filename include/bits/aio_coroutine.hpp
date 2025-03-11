#pragma once

#include <functional>
#include <memory>
#include <type_traits>

#include "context.hpp"
#include "util.hpp"

namespace AIO {

    namespace _impl {

        static inline thread_local void *volatile current_coroutine = nullptr;
        static inline constexpr std::size_t COROUTINE_STACK_SIZE = 16 * 1024; // 16 KiB

        template<typename Ret, typename Arg>
        struct MetaActualSignature {
            using Type = Ret(Arg);
        };

        template<typename Ret>
        struct MetaActualSignature<Ret, void> {
            using Type = Ret();
        };

        template<typename Ret, typename Arg>
        using MetaActualSignatureT = typename MetaActualSignature<Ret, Arg>::Type;

        template<typename Ret, typename Arg, typename Derived>
        class CoroutineBase;

        class CoroutineKiller {
        public:
            CoroutineKiller();
            ~CoroutineKiller() noexcept(false);

        private:
            template<typename Ret, typename Arg, typename Derived>
            friend class CoroutineBase;

            bool caught = false;
        };

        template<typename Ret, typename Arg, typename Derived>
        class CoroutineBase {
        public:
            template<typename Functor, typename FunctorDecay = std::decay_t<Functor>>
                requires(!std::is_same_v<FunctorDecay, CoroutineBase>)
            /* implicit */ CoroutineBase(Functor &&fun); // NOLINT(*-explicit-constructor)

            CoroutineBase(const CoroutineBase &) = delete;
            CoroutineBase(CoroutineBase &&other) = delete;

            CoroutineBase &operator=(const CoroutineBase &) = delete;
            CoroutineBase &operator=(CoroutineBase &&) = delete;

            template<typename... ResumeArgs>
            Ret resume(ResumeArgs &&...arg);

            template<typename... YieldRets>
            Arg yield(YieldRets &&...ret);

            [[nodiscard]] bool is_dead() const;

            void kill();

            ~CoroutineBase();

        private:
            using SignatureT = MetaActualSignatureT<Ret, Arg>;

            [[noreturn]] static void entrypoint() noexcept;

            void yield_error_impl();

        protected:
            enum class State : uint8_t { RUN = 0, FINISH = 1, ERROR = 2 };

            std::unique_ptr<char[]> prepare_stack();

            void check_rethrow();

            void check_kill();

            aio_context ctx{};
            State state = State::RUN;

            std::move_only_function<SignatureT> fun;
            std::unique_ptr<char[]> stack;
        };

    } // namespace _impl

    template<typename Signature>
    class Coroutine;

    template<typename Ret, typename Arg>
    class Coroutine<Ret(Arg)> final : public _impl::CoroutineBase<Ret, Arg, Coroutine<Ret(Arg)>> {
        using Base = _impl::CoroutineBase<Ret, Arg, Coroutine>;

    public:
        using Base::Base;

    private:
        friend Base;

        template<typename ResumeArg>
        Ret resume_impl(ResumeArg &&arg);
        template<typename YieldRet>
        Arg yield_impl(YieldRet &&ret, bool finish);

        static void entrypoint();

        using RetV = std::remove_reference_t<Ret>;
        using ArgV = std::remove_reference_t<Arg>;

        RetV *ret = nullptr;
        ArgV *arg = nullptr;
    };

    template<typename Arg>
    class Coroutine<void(Arg)> final : public _impl::CoroutineBase<void, Arg, Coroutine<void(Arg)>> {
        using Base = _impl::CoroutineBase<void, Arg, Coroutine>;

    public:
        using Base::Base;

    private:
        friend Base;

        template<typename ResumeArg>
        void resume_impl(ResumeArg &&arg);
        Arg yield_impl(bool finish);

        static void entrypoint();

        using ArgV = std::remove_reference_t<Arg>;

        ArgV *arg = nullptr;
    };

    template<typename Ret>
    class Coroutine<Ret()> final : public _impl::CoroutineBase<Ret, void, Coroutine<Ret()>> {
        using Base = _impl::CoroutineBase<Ret, void, Coroutine>;

    public:
        using Base::Base;

    private:
        friend Base;

        Ret resume_impl();
        template<typename YieldRet>
        void yield_impl(YieldRet &&ret, bool finish);

        static void entrypoint();

        using RetV = std::remove_reference_t<Ret>;

        RetV *ret = nullptr;
    };

    template<>
    class Coroutine<void()> final : public _impl::CoroutineBase<void, void, Coroutine<void()>> {
        using Base = CoroutineBase;

    public:
        using Base::Base;

    private:
        friend Base;

        void resume_impl();
        void yield_impl(bool finish);

        static void entrypoint();
    };

    class EndGeneration final : public std::exception {
    public:
        EndGeneration();

        [[nodiscard]] const char *what() const noexcept override;
    };

    struct CoroutineIteratorEnd final {};

    template<typename Ret>
    class CoroutineIterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Ret;
        using pointer = Ret *;
        using reference = Ret &;
        using difference_type = std::ptrdiff_t;

        explicit CoroutineIterator(Coroutine<Ret()> &coro);

        // ReSharper disable once CppNonExplicitConvertingConstructor
        /* implicit */ CoroutineIterator(CoroutineIteratorEnd); // NOLINT(*-explicit-constructor)

        CoroutineIterator(const CoroutineIterator &other);
        CoroutineIterator &operator=(const CoroutineIterator &other);

        CoroutineIterator(CoroutineIterator &&) = delete;
        CoroutineIterator &operator=(CoroutineIterator &&) = delete;

        Ret &operator*() const;
        Ret *operator->() const;

        CoroutineIterator &operator++();

        bool operator==(const CoroutineIterator &other) const;
        bool operator!=(const CoroutineIterator &other) const;

    private:
        void obtain_value() const;

        mutable Coroutine<Ret()> *coro = nullptr;
        mutable std::optional<Ret> holder = std::nullopt;
    };

    template<typename Ret>
    class CoroutineGenerator {
    public:
        // ReSharper disable once CppNonExplicitConvertingConstructor
        CoroutineGenerator(Coroutine<Ret()> &coro); // NOLINT(*-explicit-constructor)
        CoroutineGenerator();

        CoroutineIterator<Ret> begin() const;
        CoroutineIterator<Ret> end() const;

    private:
        Coroutine<Ret()> *coro = nullptr;
    };

} // namespace AIO
