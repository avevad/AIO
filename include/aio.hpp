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

        static const constexpr size_t COROUTINE_STACK_SIZE_BYTES = 64 * 1024 * 1024;

        thread_local void *volatile current_coroutine = nullptr;


        [[noreturn]] void assertion_failed(const std::string &what,
                                           std::source_location where = std::source_location::current()) {
            std::cerr << std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                         std::to_string(where.column()) + ": function ‘" + where.function_name() +
                         "’: assertion failed: " + what << std::endl;
            std::abort();
        }

        struct coroutine_finish {
        };

        struct coroutine_void_t {
        };

        template<typename Signature, typename Derived>
        class CoroutineBase;

        struct coroutine_kill {
        private:
            bool handled = false;

            template<typename Signature, typename Derived>
            friend
            class CoroutineBase;

        public:
            ~coroutine_kill() noexcept(false) {
                if (!handled) throw coroutine_kill(); // NOLINT(*-exception-baseclass)
            }
        };

        template<typename Ret, typename Arg, typename Derived>
        class CoroutineBase<Ret(Arg), Derived> {
            ucontext_t context{}, ret_context{};
            std::optional<std::tuple<Arg &>> arg_ref = std::nullopt;
            std::optional<std::tuple<Ret &>> ret_ref = std::nullopt;
            std::vector<char> stack{};

            bool dead = false;

        public:
            CoroutineBase() : stack(COROUTINE_STACK_SIZE_BYTES) {
                getcontext(&context);
                context.uc_link = nullptr;
                context.uc_stack.ss_sp = stack.data();
                context.uc_stack.ss_size = COROUTINE_STACK_SIZE_BYTES;
                makecontext(&context, entrypoint, 0);
            }

            CoroutineBase(CoroutineBase &&other) noexcept: CoroutineBase() {
                this->swap(other);
            }


            CoroutineBase &operator=(CoroutineBase &&other) noexcept {
                using std::move_only_function<Ret(Arg)>::operator=;
                if (&other == this) return *this;
                CoroutineBase junk;
                this->swap(junk);
                this->swap(other);
                return *this;
            }

            CoroutineBase &operator=(const CoroutineBase &coroutine) = delete;

            ~CoroutineBase() {
                if (!dead) kill();
                swapcontext(&ret_context, &context);
            }

            void swap(CoroutineBase &other) noexcept {
                using std::move_only_function<Ret(Arg)>::swap;
                std::swap(context, other.context);
                std::swap(ret_context, other.ret_context);
                std::swap(arg_ref, other.arg_ref);
                std::swap(ret_ref, other.ret_ref);
                std::swap(stack, other.stack);
                std::swap(dead, other.dead);
            }

            bool is_dead() { return dead; }

            void kill() {
                if (dead) assertion_failed("attempt to kill dead coroutine");
                if (current_coroutine == this) {
                    throw coroutine_kill(); // NOLINT(*-exception-baseclass)
                } else {
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
            }

        protected:
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

        private:
            void start() {
                try {
                    auto &arg = std::get<0>(arg_ref.value());
                    arg_ref.reset();
                    yield_impl((*static_cast<Derived *>(this))(arg), true);
                } catch (coroutine_finish) {
                } catch (...) {
                    try {
                        yield_err();
                    } catch (coroutine_finish) {}
                }
                swapcontext(&context, &ret_context);
            }

            static void entrypoint() {
                reinterpret_cast<CoroutineBase *>(current_coroutine)->start();
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

//    template<typename Ret, typename Arg>
//    class Coroutine<Ret(Arg)> : public _impl::CoroutineBase<Ret(Arg)> {
//    public:
//        using _impl::CoroutineBase<Ret(Arg)>::_impl::CoroutineImpl;
//
//        using _impl::CoroutineBase<Ret(Arg)>::is_dead;
//        using _impl::CoroutineBase<Ret(Arg)>::kill;
//    };

    template<typename Ret>
    class Coroutine<Ret()> : _impl::CoroutineBase<Ret(_impl::coroutine_void_t), Coroutine<Ret()>>, std::move_only_function<Ret(_impl::coroutine_void_t)> {
        friend _impl::CoroutineBase<Ret(_impl::coroutine_void_t), Coroutine>;
    public:
        template<typename Functor, typename FunctorDec = std::decay_t<Functor>>
        requires (!std::is_same_v<FunctorDec, Coroutine>)
        Coroutine(Functor &&start_fn) : // NOLINT(*-explicit-constructor)
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

//    template<typename Arg>
//    class Coroutine<void(Arg)> : private _impl::CoroutineBase<_impl::coroutine_void_t(Arg)> {
//    public:
//        template<typename Functor, typename FunctorDec = std::decay_t<Functor>>
//        requires (!std::is_same_v<FunctorDec, Coroutine>)
//        Coroutine(Functor &&start_fn) : // NOLINT(*-explicit-constructor)
//                _impl::CoroutineBase<_impl::coroutine_void_t(Arg)>(
//                        [start_fn = std::forward<Functor>(start_fn)](auto arg) -> _impl::coroutine_void_t {
//                            start_fn(arg);
//                            return {};
//                        }) {}
//
//        template<typename ResumeArg>
//        void resume(ResumeArg &&arg) {
//            _impl::CoroutineBase<_impl::coroutine_void_t(Arg)>::resume_impl(arg);
//        }
//
//        Arg yield() {
//            return _impl::CoroutineBase<_impl::coroutine_void_t(Arg)>::yield_impl(_impl::coroutine_void_t{});
//        }
//
//        using _impl::CoroutineBase<_impl::coroutine_void_t(Arg)>::is_dead;
//        using _impl::CoroutineBase<_impl::coroutine_void_t(Arg)>::kill;
//    };
//
//    template<>
//    class Coroutine<void()> : private _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t)> {
//    public:
//        template<typename Functor, typename FunctorDec = std::decay_t<Functor>>
//        requires (!std::is_same_v<FunctorDec, Coroutine>)
//        Coroutine(Functor &&start_fn) : // NOLINT(*-explicit-constructor)
//                _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t)>(
//                        [start_fn = std::forward<Functor>(start_fn)](_impl::coroutine_void_t) -> _impl::coroutine_void_t {
//                            start_fn();
//                            return {};
//                        }) {}
//
//        void resume() {
//            _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t)>::resume_impl(
//                    _impl::coroutine_void_t{});
//        }
//
//        void yield() {
//            _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t)>::yield_impl(
//                    _impl::coroutine_void_t{});
//        }
//
//        using _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t)>::is_dead;
//        using _impl::CoroutineBase<_impl::coroutine_void_t(_impl::coroutine_void_t)>::kill;
//
//    };
//
//    class EventLoop;
//
//    template<typename Ret>
//    class Promise;
//
//    template<typename Ret>
//    class Future final : _impl::Bond {
//        EventLoop *loop;
//        std::optional<Ret> ret;
//        Coroutine<void()> prod, *cons = nullptr;
//
//        friend EventLoop;
//        friend Promise<Ret>;
//
//        explicit Future(EventLoop *loop, Coroutine<void()> cor) : loop(loop), prod(std::move(cor)) {}
//
//        Future(Future &&) = default;
//
//        Future &operator=(Future &&) = default;
//
//    public:
//        Ret await();
//
//        ~Future() override {
//            if (!cons) _impl::assertion_failed("future was never awaited");
//        }
//    };
//
//
//    template<typename Ret>
//    class Promise final : _impl::Bond {
//        friend EventLoop;
//
//        Promise() = default;
//
//    public:
//        Future<Ret> &future() const {
//            return *static_cast<Future<Ret> *>(get_link());
//        }
//    };
//
//    class EventLoop {
//        template<typename Ret>
//        friend
//        class Future;
//
//    protected:
//        virtual void set_current_coroutine(Coroutine<void()> *cor) = 0;
//
//        virtual Coroutine<void()> *get_current_coroutine() = 0;
//
//        Coroutine<void()> *swap_coroutine(Coroutine<void()> *cor) {
//            Coroutine<void()> *cur = get_current_coroutine();
//            set_current_coroutine(cor);
//            return cur;
//        }
//
//    public:
//        virtual void add_task(const std::move_only_function<void()> &fn) = 0;
//
//        template<typename F, typename... Args>
//        Future<std::result_of_t<F(Args...)>> async_call(F fn, Args &&... args) {
//            Promise<std::result_of_t<F(Args...)>> promise;
//            Future<std::result_of_t<F(Args...)>> future(this, [this, promise = std::move(promise), fn = std::move(fn), args_tup = std::make_tuple(args...)]() -> void {
//                promise.future().ret = std::apply(fn, args_tup);
//                add_task([this, promise = Promise<int>()]() -> void {
//                    set_current_coroutine(&promise.future().prod);
//                    promise.future().prod.resume();
//                    set_current_coroutine(nullptr);
//                });
//            });
//            _impl::Bond::bind(future, promise);
//            return future;
//        }
//
//        template<typename Ret, typename... Args>
//        std::move_only_function<Future<Ret>(Args...)> async(const std::move_only_function<Ret(Args...)> &fn) {
//            return [this, &fn](Args &&... args) -> Ret { return async_call(fn); };
//        }
//    };
//
//    template<typename Ret>
//    Ret Future<Ret>::await() {
//        if (cons) _impl::assertion_failed("future was already awaited");
//        cons = loop->get_current_coroutine();
//        if (!cons) _impl::assertion_failed("await() in synchronous context");
//        cons->yield();
//        return std::move(ret.value());
//    }
}