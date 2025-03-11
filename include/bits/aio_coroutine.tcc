#pragma once

#include "aio_coroutine.hpp"

#include <utility>

namespace AIO::_impl {

    inline CoroutineKiller::CoroutineKiller() = default;

    inline CoroutineKiller::~CoroutineKiller() noexcept(false) {
        if (!caught) {
            throw CoroutineKiller(); // NOLINT(*-exception-baseclass)
        }
    }

    template<typename Ret, typename Arg, typename Derived>
    template<typename Functor, typename FunctorDecay>
        requires(!std::is_same_v<FunctorDecay, CoroutineBase<Ret, Arg, Derived>>)
    CoroutineBase<Ret, Arg, Derived>::CoroutineBase(Functor &&fun)
        : fun(std::forward<Functor>(fun)), stack(std::make_unique<char[]>(COROUTINE_STACK_SIZE)),
          ctx(make_context(entrypoint, stack.get(), COROUTINE_STACK_SIZE)) {
    }

    template<typename Ret, typename Arg, typename Derived>
    template<typename... ResumeArgs>
    Ret CoroutineBase<Ret, Arg, Derived>::resume(ResumeArgs &&...arg) {
        if (current_coroutine == this)
            assertion_failed("attempt to resume current coroutine");
        if (is_dead())
            assertion_failed("attempt to resume dead coroutine");

        return static_cast<Derived *>(this)->resume_impl(std::forward<ResumeArgs>(arg)...);
    }

    template<typename Ret, typename Arg, typename Derived>
    template<typename... YieldRets>
    Arg CoroutineBase<Ret, Arg, Derived>::yield(YieldRets &&...ret) {
        if (current_coroutine != this)
            assertion_failed("attempt to yield another coroutine");

        return static_cast<Derived *>(this)->yield_impl(std::forward<YieldRets>(ret)..., false);
    }

    template<typename Ret, typename Arg, typename Derived>
    bool CoroutineBase<Ret, Arg, Derived>::is_dead() const {
        return state != State::RUN;
    }

    template<typename Ret, typename Arg, typename Derived>
    void CoroutineBase<Ret, Arg, Derived>::kill() {
        if (current_coroutine == this)
            assertion_failed("attempt to kill current coroutine");
        if (is_dead())
            assertion_failed("attempt to kill dead coroutine");

        state = State::ERROR;

        void *prev_coroutine = current_coroutine;
        current_coroutine = this;
        switch_to_context(ctx);
        current_coroutine = prev_coroutine;

        try {
            throw;
        } catch (CoroutineKiller &killer) {
            killer.caught = true;
        }
    }

    template<typename Ret, typename Arg, typename Derived>
    CoroutineBase<Ret, Arg, Derived>::~CoroutineBase() {
        if (!is_dead()) {
            kill();
        }
    }

    template<typename Ret, typename Arg, typename Derived>
    context_t &CoroutineBase<Ret, Arg, Derived>::entrypoint() noexcept {
        try {
            Derived::entrypoint();
        } catch (...) {
            static_cast<CoroutineBase *>(current_coroutine)->yield_error_impl();
        }
        std::unreachable();
    }

    template<typename Ret, typename Arg, typename Derived>
    void CoroutineBase<Ret, Arg, Derived>::yield_error_impl() {
        state = State::ERROR;

        switch_to_context(ctx);
    }

    template<typename Ret, typename Arg, typename Derived>
    void CoroutineBase<Ret, Arg, Derived>::check_rethrow() {
        if (state == State::ERROR) {
            throw;
        }
    }

    template<typename Ret, typename Arg, typename Derived>
    void CoroutineBase<Ret, Arg, Derived>::check_kill() {
        if (state == State::ERROR) {
            throw CoroutineKiller(); // NOLINT(*-exception-baseclass)
        }
    }

} // namespace AIO::_impl

namespace AIO {

    template<typename Ret, typename Arg>
    template<typename ResumeArg>
    Ret Coroutine<Ret(Arg)>::resume_impl(ResumeArg &&arg) {
        this->arg = &arg;

        void *prev_coroutine = _impl::current_coroutine;
        _impl::current_coroutine = this;
        switch_to_context(Base::ctx);
        _impl::current_coroutine = prev_coroutine;

        Base::check_rethrow();

        return *ret;
    }

    template<typename Ret, typename Arg>
    template<typename YieldRet>
    Arg Coroutine<Ret(Arg)>::yield_impl(YieldRet &&ret, const bool finish) {
        if (finish)
            Base::state = Base::State::FINISH;

        this->ret = &ret;

        switch_to_context(Base::ctx);

        Base::check_kill();

        return *arg;
    }

    template<typename Ret, typename Arg>
    void Coroutine<Ret(Arg)>::entrypoint() {
        auto *self = static_cast<Coroutine *>(_impl::current_coroutine);
        self->yield_impl(self->fun(*self->arg), true);
    }

    template<typename Arg>
    template<typename ResumeArg>
    void Coroutine<void(Arg)>::resume_impl(ResumeArg &&arg) {
        this->arg = &arg;

        void *prev_coroutine = _impl::current_coroutine;
        _impl::current_coroutine = this;
        switch_to_context(Base::ctx);
        _impl::current_coroutine = prev_coroutine;

        Base::check_rethrow();
    }

    template<typename Arg>
    Arg Coroutine<void(Arg)>::yield_impl(const bool finish) {
        if (finish)
            Base::state = Base::State::FINISH;

        switch_to_context(Base::ctx);

        Base::check_kill();

        return *arg;
    }

    template<typename Arg>
    void Coroutine<void(Arg)>::entrypoint() {
        auto *self = static_cast<Coroutine *>(_impl::current_coroutine);
        self->fun(*self->arg);
        self->yield_impl(true);
    }

    template<typename Ret>
    Ret Coroutine<Ret()>::resume_impl() {
        void *prev_coroutine = _impl::current_coroutine;
        _impl::current_coroutine = this;
        switch_to_context(Base::ctx);
        _impl::current_coroutine = prev_coroutine;

        Base::check_rethrow();

        return *ret;
    }

    template<typename Ret>
    template<typename YieldRet>
    void Coroutine<Ret()>::yield_impl(YieldRet &&ret, const bool finish) {
        if (finish)
            Base::state = Base::State::FINISH;

        this->ret = &ret;

        switch_to_context(Base::ctx);

        Base::check_kill();
    }

    template<typename Ret>
    void Coroutine<Ret()>::entrypoint() {
        auto *self = static_cast<Coroutine *>(_impl::current_coroutine);
        self->yield_impl(self->fun(), true);
    }

    inline void Coroutine<void()>::resume_impl() {
        void *prev_coroutine = _impl::current_coroutine;
        _impl::current_coroutine = this;
        switch_to_context(ctx);
        _impl::current_coroutine = prev_coroutine;

        check_rethrow();
    }

    inline void Coroutine<void()>::yield_impl(const bool finish) {
        if (finish)
            state = State::FINISH;

        switch_to_context(ctx);

        check_kill();
    }

    inline void Coroutine<void()>::entrypoint() {
        auto *self = static_cast<Coroutine *>(_impl::current_coroutine);
        self->fun();
        self->yield_impl(true);
    }

    inline EndGeneration::EndGeneration() = default;

    inline const char *EndGeneration::what() const noexcept {
        return "AIO::EndGeneration";
    }

    template<typename Ret>
    CoroutineIterator<Ret>::CoroutineIterator(Coroutine<Ret()> &coro) : coro(&coro) {
    }

    template<typename Ret>
    CoroutineIterator<Ret>::CoroutineIterator(CoroutineIteratorEnd) {
    }

    template<typename Ret>
    CoroutineIterator<Ret>::CoroutineIterator(const CoroutineIterator &other) : coro(other.coro) {
    }

    template<typename Ret>
    CoroutineIterator<Ret> &CoroutineIterator<Ret>::operator=(const CoroutineIterator &other) {
        if (&other == this)
            return *this;

        coro = other.coro;
        return *this;
    }

    template<typename Ret>
    Ret &CoroutineIterator<Ret>::operator*() const {
        return *this->operator->();
    }

    template<typename Ret>
    Ret *CoroutineIterator<Ret>::operator->() const {
        obtain_value();

        if (!coro)
            assertion_failed("dereferencing singular iterator");

        return &holder.value();
    }

    template<typename Ret>
    CoroutineIterator<Ret> &CoroutineIterator<Ret>::operator++() {
        obtain_value();

        if (!coro)
            assertion_failed("incrementing singular iterator");

        holder.reset();

        return *this;
    }

    template<typename Ret>
    bool CoroutineIterator<Ret>::operator==(const CoroutineIterator &other) const {
        obtain_value();

        return coro == nullptr && other.coro == nullptr;
    }

    template<typename Ret>
    bool CoroutineIterator<Ret>::operator!=(const CoroutineIterator &other) const {
        return !(*this == other);
    }

    template<typename Ret>
    void CoroutineIterator<Ret>::obtain_value() const {
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

    template<typename Ret>
    CoroutineGenerator<Ret>::CoroutineGenerator(Coroutine<Ret()> &coro) : coro(&coro) {
    }

    template<typename Ret>
    CoroutineGenerator<Ret>::CoroutineGenerator() = default;

    template<typename Ret>
    CoroutineIterator<Ret> CoroutineGenerator<Ret>::begin() const {
        return coro ? CoroutineIterator(*coro) : CoroutineIteratorEnd();
    }

    template<typename Ret>
    CoroutineIterator<Ret> CoroutineGenerator<Ret>::end() const {
        return CoroutineIteratorEnd();
    }

} // namespace AIO
