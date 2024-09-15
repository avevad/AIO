#ifndef COROUTINE_H
#define COROUTINE_H

#include <type_traits>
#include <functional>
#include <memory>

#include "util.hpp"
#include "context.hpp"

namespace AIO {

    namespace _impl {

        static inline thread_local void *volatile current_coroutine = nullptr;

        static constexpr std::size_t COROUTINE_STACK_SIZE = 16 * 1024; // 16 KiB

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
            CoroutineKiller() = default;

            ~CoroutineKiller() noexcept(false) {
                if (!caught) {
                    throw CoroutineKiller(); // NOLINT(*-exception-baseclass)
                }
            }

        private:
            template<typename Ret, typename Arg, typename Derived>
            friend class CoroutineBase;

            bool caught = false;
        };

        template<typename Ret, typename Arg, typename Derived>
        class CoroutineBase {
        public:
            template<typename Functor, typename FunctorDecay = std::decay_t<Functor> >
                requires(!std::is_same_v<FunctorDecay, CoroutineBase>)
            /* implicit */ CoroutineBase(Functor &&fun) //NOLINT(*-explicit-constructor)
                : fun(std::forward<Functor>(fun)), stack(prepare_stack()) { }

            CoroutineBase(const CoroutineBase &) = delete;
            CoroutineBase(CoroutineBase &&other) = delete;

            CoroutineBase &operator=(const CoroutineBase &) = delete;
            CoroutineBase &operator=(CoroutineBase &&) = delete;

            template<typename ...ResumeArgs>
            Ret resume(ResumeArgs && ...arg) {
                if (current_coroutine == this)
                    assertion_failed("attempt to resume current coroutine");
                if (is_dead())
                    assertion_failed("attempt to resume dead coroutine");

                return static_cast<Derived *>(this)->resume_impl(std::forward<ResumeArgs>(arg) ...);
            }

            template<typename ...YieldRets>
            Arg yield(YieldRets && ...ret) {
                if (current_coroutine != this)
                    assertion_failed("attempt to yield another coroutine");

                return static_cast<Derived *>(this)->yield_impl(std::forward<YieldRets>(ret) ..., false);
            }

            [[nodiscard]] bool is_dead() const {
                return state != State::RUN;
            }

            void kill() {
                if (current_coroutine == this)
                    assertion_failed("attempt to kill current coroutine");
                if (is_dead())
                    assertion_failed("attempt to kill dead coroutine");

                state = State::ERROR;

                void *prev_coroutine = current_coroutine;
                current_coroutine = this;
                aio_context_switch(&ctx);
                current_coroutine = prev_coroutine;

                try {
                    throw;
                } catch (CoroutineKiller &killer) {
                    killer.caught = true;
                }
            }

            ~CoroutineBase() {
                if (!is_dead()) {
                    kill();
                }
            }

        private:
            using SignatureT = MetaActualSignatureT<Ret, Arg>;

            [[noreturn]] static void entrypoint() noexcept {
                try {
                    Derived::entrypoint();
                } catch (...) {
                    static_cast<CoroutineBase *>(current_coroutine)->yield_error_impl();
                }
                assertion_failed("coroutine entrypoint return trap");
            }

            void yield_error_impl() {
                state = State::ERROR;

                aio_context_switch(&ctx);
            }

        protected:
            enum class State : uint8_t {
                RUN = 0, FINISH = 1, ERROR = 2
            };

            std::unique_ptr<char[]> prepare_stack() {
                auto stack = std::make_unique<char[]>(COROUTINE_STACK_SIZE);

                auto *stack_top = stack.get() + COROUTINE_STACK_SIZE - sizeof(void *);
                aio_context_create(&ctx, stack_top, entrypoint);

                return stack;
            }

            void check_rethrow() {
                if (state == State::ERROR) {
                    throw;
                }
            }

            void check_kill() {
                if (state == State::ERROR) {
                    throw CoroutineKiller(); // NOLINT(*-exception-baseclass)
                }
            }

            aio_context ctx { };
            State state = State::RUN;

            std::move_only_function<SignatureT> fun;
            std::unique_ptr<char[]> stack;
        };

    }

    template<typename Signature>
    class Coroutine;

    template<typename Ret, typename Arg>
    class Coroutine<Ret(Arg)> final : public _impl::CoroutineBase<Ret, Arg, Coroutine<Ret(Arg)> > {
        using Base = _impl::CoroutineBase<Ret, Arg, Coroutine>;

    public:
        using Base::Base;

    private:
        friend Base;

        template<typename ResumeArg>
        Ret resume_impl(ResumeArg &&arg) {
            this->arg = &arg;

            void *prev_coroutine = _impl::current_coroutine;
            _impl::current_coroutine = this;
            aio_context_switch(&(Base::ctx));
            _impl::current_coroutine = prev_coroutine;

            Base::check_rethrow();

            return *ret;
        }

        template<typename YieldRet>
        Arg yield_impl(YieldRet &&ret, const bool finish) {
            if (finish)
                Base::state = Base::State::FINISH;

            this->ret = &ret;

            aio_context_switch(&(Base::ctx));

            Base::check_kill();

            return *arg;
        }

        static void entrypoint() {
            auto *self = static_cast<Coroutine *>(_impl::current_coroutine);
            self->yield_impl(self->fun(*self->arg), true);
        }

        using RetV = std::remove_reference_t<Ret>;
        using ArgV = std::remove_reference_t<Arg>;

        RetV *ret = nullptr;
        ArgV *arg = nullptr;
    };

    template<typename Arg>
    class Coroutine<void(Arg)> final : public _impl::CoroutineBase<void, Arg, Coroutine<void(Arg)> > {
        using Base = _impl::CoroutineBase<void, Arg, Coroutine>;

    public:
        using Base::Base;

    private:
        friend Base;

        template<typename ResumeArg>
        void resume_impl(ResumeArg &&arg) {
            this->arg = &arg;

            void *prev_coroutine = _impl::current_coroutine;
            _impl::current_coroutine = this;
            aio_context_switch(&(Base::ctx));
            _impl::current_coroutine = prev_coroutine;

            Base::check_rethrow();
        }

        Arg yield_impl(const bool finish) {
            if (finish)
                Base::state = Base::State::FINISH;

            aio_context_switch(&(Base::ctx));

            Base::check_kill();

            return *arg;
        }

        static void entrypoint() {
            auto *self = static_cast<Coroutine *>(_impl::current_coroutine);
            self->fun(*self->arg);
            self->yield_impl(true);
        }

        using ArgV = std::remove_reference_t<Arg>;

        ArgV *arg = nullptr;
    };

    template<typename Ret>
    class Coroutine<Ret()> final : public _impl::CoroutineBase<Ret, void, Coroutine<Ret()> > {
        using Base = _impl::CoroutineBase<Ret, void, Coroutine>;

    public:
        using Base::Base;

    private:
        friend Base;

        Ret resume_impl() {
            void *prev_coroutine = _impl::current_coroutine;
            _impl::current_coroutine = this;
            aio_context_switch(&(Base::ctx));
            _impl::current_coroutine = prev_coroutine;

            Base::check_rethrow();

            return *ret;
        }

        template<typename YieldRet>
        void yield_impl(YieldRet &&ret, const bool finish) {
            if (finish)
                Base::state = Base::State::FINISH;

            this->ret = &ret;

            aio_context_switch(&(Base::ctx));

            Base::check_kill();
        }

        static void entrypoint() {
            auto *self = static_cast<Coroutine *>(_impl::current_coroutine);
            self->yield_impl(self->fun(), true);
        }

        using RetV = std::remove_reference_t<Ret>;

        RetV *ret = nullptr;
    };

    template<>
    class Coroutine<void()> final : public _impl::CoroutineBase<void, void, Coroutine<void()> > {
        using Base = CoroutineBase;

    public:
        using Base::Base;

    private:
        friend Base;

        void resume_impl() {
            void *prev_coroutine = _impl::current_coroutine;
            _impl::current_coroutine = this;
            aio_context_switch(&ctx);
            _impl::current_coroutine = prev_coroutine;

            check_rethrow();
        }

        void yield_impl(const bool finish) {
            if (finish)
                state = State::FINISH;

            aio_context_switch(&ctx);

            check_kill();
        }

        static void entrypoint() {
            auto *self = static_cast<Coroutine *>(_impl::current_coroutine);
            self->fun();
            self->yield_impl(true);
        }
    };

    class EndGeneration final : public std::exception {
    public:
        EndGeneration() = default;

        [[nodiscard]] const char *what() const noexcept override {
            return "AIO::EndGeneration";
        }
    };

    struct CoroutineIteratorEnd { };

    template<typename Ret>
    class CoroutineIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Ret;
        using pointer = Ret *;
        using reference = Ret &;
        using difference_type = std::ptrdiff_t;

        explicit CoroutineIterator(Coroutine<Ret()> &coro) : coro(&coro) { }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        CoroutineIterator(CoroutineIteratorEnd) { } //NOLINT(*-explicit-constructor)

        CoroutineIterator(const CoroutineIterator &other) : coro(other.coro) { }

        CoroutineIterator &operator=(const CoroutineIterator &other) {
            if (&other == this)
                return *this;

            coro = other.coro;
            return *this;
        }

        CoroutineIterator(CoroutineIterator &&) = delete;
        CoroutineIterator &operator=(CoroutineIterator &&) = delete;

        Ret &operator*() const {
            return *this->operator->();
        }

        Ret *operator->() const {
            obtain_value();

            if (!coro)
                assertion_failed("dereferencing singular iterator");

            return &holder.value();
        }

        CoroutineIterator &operator++() {
            obtain_value();

            if (!coro)
                assertion_failed("incrementing singular iterator");

            holder.reset();

            return *this;
        }

        bool operator==(const CoroutineIterator &other) const {
            obtain_value();

            return coro == nullptr && other.coro == nullptr;
        }

        bool operator!=(const CoroutineIterator &other) const {
            return !(*this == other);
        }

    private:
        void obtain_value() const {
            if (!coro)
                return;

            if (holder.has_value())
                return;

            if (coro->is_dead()) {
                coro = nullptr;
                holder = std::nullopt;
            }

            try {
                holder = coro->resume();
            } catch (const EndGeneration &) {
                coro = nullptr;
                holder = std::nullopt;
            }
        }

        mutable Coroutine<Ret()> *coro = nullptr;
        mutable std::optional<Ret> holder = std::nullopt;
    };

    template<typename Ret>
    class CoroutineGenerator {
    public:
        // ReSharper disable once CppNonExplicitConvertingConstructor
        CoroutineGenerator(Coroutine<Ret()> &coro) : coro(&coro) { } // NOLINT(*-explicit-constructor)

        CoroutineGenerator() = default;

        CoroutineIterator<Ret> begin() const {
            return coro ? CoroutineIterator(*coro) : CoroutineIteratorEnd();
        }

        CoroutineIterator<Ret> end() const {
            return CoroutineIteratorEnd();
        }

    private:
        Coroutine<Ret()> *coro = nullptr;
    };

}

#endif //COROUTINE_H
