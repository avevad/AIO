#pragma once

#include <iostream>
#include <functional>
#include <source_location>
#include <variant>
#include <optional>
#include <type_traits>

#include "ucontext.h"

namespace AIO {

    namespace _impl {

        static constexpr size_t COROUTINE_STACK_SIZE_BYTES = 64 * 1024 * 1024;

        inline thread_local void *volatile current_coroutine = nullptr;


        [[noreturn]] inline void assertion_failed(const std::string &what,
                                                  const std::source_location where = std::source_location::current()) {
            std::cerr << std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                         std::to_string(where.column()) + ": function ‘" + where.function_name() +
                         "’: assertion failed: " + what << std::endl;
            std::abort();
        }

        struct coroutine_finish {
        };

        struct coroutine_void_t {
        };

        template<typename Signature>
        class CoroutineCore;

        struct coroutine_kill {
        private:
            bool handled = false;

            template<typename Signature>
            friend
            class CoroutineCore;

        public:
            ~coroutine_kill() noexcept(false) {
                if (!handled) throw coroutine_kill(); // NOLINT(*-exception-baseclass)
            }
        };

        template<typename Ret, typename Arg>
        class CoroutineCore<Ret(Arg)> {
        protected:
            ucontext_t context{}, ret_context{};
            std::optional<std::tuple<Arg &>> arg_ref = std::nullopt;
            std::optional<std::tuple<Ret &>> ret_ref = std::nullopt;
            std::vector<char> stack{};

            bool dead = false;

        public:
            explicit CoroutineCore(void (*entrypoint)()) : stack(COROUTINE_STACK_SIZE_BYTES) {
                getcontext(&context);
                context.uc_link = nullptr;
                context.uc_stack.ss_sp = stack.data();
                context.uc_stack.ss_size = COROUTINE_STACK_SIZE_BYTES;
                makecontext(&context, entrypoint, 0);
            }

            CoroutineCore(CoroutineCore &&other) noexcept: CoroutineCore(nullptr) {
                this->swap(other);
            }

            CoroutineCore &operator=(CoroutineCore &&other) noexcept {
                if (&other == this) return *this;
                CoroutineCore junk;
                this->swap(junk);
                this->swap(other);
                return *this;
            }

            CoroutineCore &operator=(const CoroutineCore &coroutine) = delete;

            ~CoroutineCore() {
                if (!dead) kill();
                swapcontext(&ret_context, &context);
            }

            void swap(CoroutineCore &other) noexcept {
                std::swap(context, other.context);
                std::swap(ret_context, other.ret_context);
                std::swap(arg_ref, other.arg_ref);
                std::swap(ret_ref, other.ret_ref);
                std::swap(stack, other.stack);
                std::swap(dead, other.dead);
            }

            bool is_dead() const { return dead; }

            void kill() {
                if (dead) assertion_failed("attempt to kill dead coroutine");
                if (current_coroutine == this) throw coroutine_kill(); // NOLINT(*-exception-baseclass)
                void *prev_coroutine = current_coroutine;
                current_coroutine = this;
                swapcontext(&ret_context, &context);
                current_coroutine = prev_coroutine;
                try {
                    throw;
                } catch (coroutine_kill &kill) {
                    kill.handled = true;
                }
            }

            template<typename ResumeArg>
            Ret resume_impl(ResumeArg &&arg) {
                if (current_coroutine == this) assertion_failed("attempt to resume current coroutine");
                if (dead) assertion_failed("attempt to resume dead coroutine");
                arg_ref = std::tuple<Arg &>(const_cast<Arg &>(arg));
                void *prev_coroutine = current_coroutine;
                current_coroutine = this;
                swapcontext(&ret_context, &context);
                current_coroutine = prev_coroutine;
                if (!ret_ref.has_value()) {
                    throw;
                }
                auto &ret = std::get<0>(ret_ref.value());
                ret_ref.reset();
                return ret;
            }

            template<typename YieldRet>
            Arg yield_impl(YieldRet &&ret, bool finish = false) {
                if (current_coroutine != this) assertion_failed("attempt to yield another coroutine");
                ret_ref = std::tuple<Ret &>(const_cast<Ret &>(ret));
                if (finish) dead = true;
                swapcontext(&context, &ret_context);
                if (finish) throw coroutine_finish(); // NOLINT(*-exception-baseclass)
                if (!arg_ref.has_value()) kill();
                auto &arg = std::get<0>(arg_ref.value());
                arg_ref.reset();
                return arg;
            }

            void yield_err() {
                dead = true;
                swapcontext(&context, &ret_context);
                throw coroutine_finish(); // NOLINT(*-exception-baseclass)
            }
        };

        template<typename Signature, typename Derived>
        class CoroutineBase;

        template<typename Ret, typename Arg, typename Derived>
        class CoroutineBase<Ret(Arg), Derived> : public CoroutineCore<Ret(Arg)> {
        public:
            CoroutineBase() : CoroutineCore<Ret(Arg)>(entrypoint) {}
        private:
            void start() {
                try {
                    auto &arg = std::get<0>(CoroutineCore<Ret(Arg)>::arg_ref.value());
                    CoroutineCore<Ret(Arg)>::arg_ref.reset();
                    CoroutineCore<Ret(Arg)>::yield_impl((*static_cast<Derived *>(this))(arg), true);
                } catch (coroutine_finish) {
                } catch (...) {
                    try {
                        CoroutineCore<Ret(Arg)>::yield_err();
                    } catch (coroutine_finish) {}
                }
                swapcontext(&(CoroutineCore<Ret(Arg)>::context), &(CoroutineCore<Ret(Arg)>::ret_context));
            }

            static void entrypoint() {
                static_cast<CoroutineBase *>(current_coroutine)->start();
            }
        };

        class Bond {
            std::optional<Bond *> link;

        public:
            Bond() : link(std::nullopt) {}

            Bond(const Bond &) = delete;

            Bond(Bond &&other) noexcept: link(other.link) {
                other.link = std::nullopt;
                if (link.has_value() && link.value()) {
                    link.value()->link = this;
                }
            }

            Bond &operator=(const Bond &) = delete;

            Bond &operator=(Bond &&other) noexcept {
                if (link.has_value() && link.value()) {
                    link.value()->link = nullptr;
                }
                link = other.link;
                other.link = std::nullopt;
                if (link.has_value() && link.value()) {
                    link.value()->link = this;
                }
                return *this;
            }

            virtual ~Bond() {
                if (link.has_value() && link.value()) {
                    link.value()->link = nullptr;
                }
            }

            static void bind(Bond &obj1, Bond &obj2) {
                if (obj1.link.has_value() || obj2.link.has_value()) {
                    assertion_failed("object is already bound");
                }
                obj1.link = &obj2;
                obj2.link = &obj1;
            }

            [[nodiscard]] Bond *get_link() const {
                return link.value();
            }
        };
    }

    template<typename Signature>
    class Coroutine {
    };

    template<typename Ret, typename Arg>
    class Coroutine<Ret(Arg)> : _impl::CoroutineBase<Ret(Arg), Coroutine<Ret(Arg)>>, std::move_only_function<Ret(Arg)> {
        friend _impl::CoroutineBase<Ret(Arg), Coroutine>;
    public:
        template<typename Functor, typename FunctorDec = std::decay_t<Functor>>
        requires (!std::is_same_v<FunctorDec, Coroutine>)
        Coroutine(Functor &&start_fn) : // NOLINT(*-explicit-constructor, *-forwarding-reference-overload)
                std::move_only_function<Ret(Arg)>([start_fn = std::forward<Functor>(start_fn)](auto arg) {
                        return start_fn(arg);
                }) {}


        template<typename ResumeArg>
        Ret resume(ResumeArg &&arg) {
            return resume_impl(arg);
        }

        template<typename YieldRet>
        Arg yield(YieldRet &&ret) {
            return yield_impl(ret);
        }

        using _impl::CoroutineBase<Ret(Arg), Coroutine>::is_dead;
        using _impl::CoroutineBase<Ret(Arg), Coroutine>::kill;

    };

    template<typename Ret>
    class Coroutine<Ret()> : _impl::CoroutineBase<Ret(_impl::coroutine_void_t), Coroutine<Ret()>>, std::move_only_function<Ret(_impl::coroutine_void_t)> {
        friend _impl::CoroutineBase<Ret(_impl::coroutine_void_t), Coroutine>;
    public:
        template<typename Functor, typename FunctorDec = std::decay_t<Functor>>
        requires (!std::is_same_v<FunctorDec, Coroutine>)
        Coroutine(Functor &&start_fn) : // NOLINT(*-explicit-constructor, *-forwarding-reference-overload)
                std::move_only_function<Ret(_impl::coroutine_void_t)>(
                        [start_fn = std::forward<Functor>(start_fn)](_impl::coroutine_void_t) {
                            return start_fn();
                        }) {}


        Ret resume() {
            return _impl::CoroutineBase<Ret(_impl::coroutine_void_t), Coroutine>::resume_impl(_impl::coroutine_void_t{});
        }

        template<typename YieldRet>
        void yield(YieldRet &&ret) {
            _impl::CoroutineBase<Ret(_impl::coroutine_void_t), Coroutine>::yield_impl(ret);
        }

        using _impl::CoroutineBase<Ret(_impl::coroutine_void_t), Coroutine>::is_dead;
        using _impl::CoroutineBase<Ret(_impl::coroutine_void_t), Coroutine>::kill;
    };

    template<typename Arg>
    class Coroutine<void(Arg)> : _impl::CoroutineBase<_impl::coroutine_void_t(Arg), Coroutine<void(Arg)>>, std::move_only_function<_impl::coroutine_void_t(Arg)> {
        friend _impl::CoroutineBase<_impl::coroutine_void_t(Arg), Coroutine>;
    public:
        template<typename Functor, typename FunctorDec = std::decay_t<Functor>>
        requires (!std::is_same_v<FunctorDec, Coroutine>)
        Coroutine(Functor &&start_fn) : // NOLINT(*-explicit-constructor, *-forwarding-reference-overload)
                std::move_only_function<_impl::coroutine_void_t(Arg)>(
                        [start_fn = std::forward<Functor>(start_fn)](auto arg) -> _impl::coroutine_void_t {
                            start_fn(arg);
                            return {};
                        }) {}

        template<typename ResumeArg>
        void resume(ResumeArg &&arg) {
            _impl::CoroutineBase<_impl::coroutine_void_t(Arg), Coroutine>::resume_impl(arg);
        }

        Arg yield() {
            return _impl::CoroutineBase<_impl::coroutine_void_t(Arg), Coroutine>::yield_impl(_impl::coroutine_void_t{});
        }

        using _impl::CoroutineBase<_impl::coroutine_void_t(Arg), Coroutine>::is_dead;
        using _impl::CoroutineBase<_impl::coroutine_void_t(Arg), Coroutine>::kill;
    };

    template<>
    class Coroutine<void()> : _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t), Coroutine<void()>>, std::move_only_function<_impl::coroutine_void_t(_impl::coroutine_void_t)> {
        friend CoroutineBase;
    public:
        template<typename Functor, typename FunctorDec = std::decay_t<Functor>>
        requires (!std::is_same_v<FunctorDec, Coroutine>)
        Coroutine(Functor &&start_fn) : // NOLINT(*-explicit-constructor, *-forwarding-reference-overload)
                std::move_only_function<_impl::coroutine_void_t(_impl::coroutine_void_t)>(
                        [start_fn = std::forward<Functor>(start_fn)](_impl::coroutine_void_t) -> _impl::coroutine_void_t {
                            start_fn();
                            return {};
                        }) {}

        void resume() {
            resume_impl(_impl::coroutine_void_t{});
        }

        void yield() {
            yield_impl(_impl::coroutine_void_t{});
        }

        using CoroutineBase::is_dead;
        using CoroutineBase::kill;

    };

    class EventLoop;

    template<typename Ret>
    class Promise;

    template<typename Ret>
    class Future final : _impl::Bond, _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t), Future<Ret>> {
        friend EventLoop;
        friend Promise<Ret>;
        friend _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t), Future<Ret>>;

        EventLoop *loop;
        std::optional<Ret> ret;
        _impl::CoroutineCore<_impl::coroutine_void_t(_impl::coroutine_void_t)> *cons = nullptr;
        std::move_only_function<Ret()> fn;

        template<typename Functor>
        explicit Future(EventLoop *loop, Functor &&fn) : loop(loop), fn(std::forward<Functor>(fn)) {}

        Future(Future &&) = default;

        Future &operator=(Future &&) = default;

        _impl::coroutine_void_t operator()(_impl::coroutine_void_t);

    public:
        Ret await();

        ~Future() override {
            if (!cons) _impl::assertion_failed("future was never awaited");
        }
    };

    template<typename Ret>
    class Promise final : _impl::Bond {
        friend EventLoop;

        Promise() = default;

    public:
        Future<Ret> &future() const {
            return *static_cast<Future<Ret> *>(get_link());
        }
    };

    class EventLoop {
        using FutureCoroutine = _impl::CoroutineCore<_impl::coroutine_void_t(_impl::coroutine_void_t)>;

        template<typename Ret>
        friend
        class Future;

    protected:
        virtual void set_current_coroutine(FutureCoroutine *cor) = 0;

        virtual FutureCoroutine *get_current_coroutine() = 0;

    public:
        virtual void add_task(const std::move_only_function<void()> &fn) = 0;

        template<typename Functor, typename... Args>
        Future<std::result_of_t<Functor(Args...)>> async_call(Functor &&fn, Args &&... args) {
            Future<std::result_of_t<Functor(Args...)>> future(this, [fn = std::forward<Functor>(fn), args = std::tuple<Args...>(std::forward<Args>(args)...)]() -> std::result_of_t<Functor(Args...)> {
                return std::apply(fn, args);
            });
            Promise<std::result_of_t<Functor(Args...)>> promise;
            _impl::Bond::bind(future, promise);
            add_task([this, promise = std::move(promise)] () -> void {
                set_current_coroutine(&promise.future());
                promise.future().resume_impl(_impl::coroutine_void_t{});
                set_current_coroutine(nullptr);
            });
            return future;
        }
    };

    template<typename Ret>
    _impl::coroutine_void_t Future<Ret>::operator()(_impl::coroutine_void_t) {
        ret = fn();
        loop->add_task([ev_loop = this->loop, cons_cor = this->cons]() -> void {
            ev_loop->set_current_coroutine(cons_cor);
            cons_cor->resume_impl(_impl::coroutine_void_t{});
            ev_loop->set_current_coroutine(nullptr);
        });
        return {};
    }

    template<typename Ret>
    Ret Future<Ret>::await() {
        if (cons) _impl::assertion_failed("future was already awaited");
        cons = loop->get_current_coroutine();
        if (!cons) _impl::assertion_failed("await() in synchronous context");
        cons->yield_impl(_impl::coroutine_void_t{});
        return std::move(ret.value());
    }
}