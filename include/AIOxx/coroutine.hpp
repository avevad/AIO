#pragma once

#include <functional>
#include <memory>
#include <type_traits>

#include "context.hpp"
#include "coroutine.hpp"
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
      requires(!std::is_same_v<FunctorDecay, CoroutineBase<Ret, Arg, Derived>>)
    /* implicit */ CoroutineBase(Functor &&fun); // NOLINT(*-explicit-constructor)

    CoroutineBase(const CoroutineBase &) = delete;
    CoroutineBase(CoroutineBase &&other) = delete;

    CoroutineBase &operator=(const CoroutineBase &) = delete;
    CoroutineBase &operator=(CoroutineBase &&other) = delete;

    template<typename... ResumeArgs>
    Ret resume(ResumeArgs &&...arg);

    template<typename... YieldRets>
    Arg yield(YieldRets &&...ret);

    [[nodiscard]] bool is_dead() const;

    void kill();

    ~CoroutineBase();

  private:
    using SignatureT = MetaActualSignatureT<Ret, Arg>;

    [[noreturn]] static context_t &entrypoint() noexcept;

    void yield_error_impl();

  protected:
    enum class State : uint8_t { RUN = 0, FINISH = 1, ERROR = 2 };

    void check_rethrow();

    void check_kill();

    std::move_only_function<SignatureT> fun;
    std::unique_ptr<char[]> stack;

    context_t ctx{};
    State state = State::RUN;
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

  CoroutineIterator(CoroutineIterator &&) = default;
  CoroutineIterator &operator=(CoroutineIterator &&) = default;

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


// --------------------------------------------------
// -------------- TEMPLATE DEFINITIONS --------------
// --------------------------------------------------


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
  AIOXX_ASSUME(current_coroutine != this);
  AIOXX_ASSUME(!is_dead());

  return static_cast<Derived *>(this)->resume_impl(std::forward<ResumeArgs>(arg)...);
}

template<typename Ret, typename Arg, typename Derived>
template<typename... YieldRets>
Arg CoroutineBase<Ret, Arg, Derived>::yield(YieldRets &&...ret) {
  AIOXX_ASSUME(current_coroutine == this);

  return static_cast<Derived *>(this)->yield_impl(std::forward<YieldRets>(ret)..., false);
}

template<typename Ret, typename Arg, typename Derived>
bool CoroutineBase<Ret, Arg, Derived>::is_dead() const {
  return state != State::RUN;
}

template<typename Ret, typename Arg, typename Derived>
void CoroutineBase<Ret, Arg, Derived>::kill() {
  AIOXX_ASSUME(current_coroutine != this);
  AIOXX_ASSUME(!is_dead());

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
  AIOXX_ASSUME(is_dead());
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
  self->check_kill();
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
  self->check_kill();
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
  self->check_kill();
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
  self->check_kill();
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

  AIOXX_ASSUME(coro);

  return &holder.value();
}

template<typename Ret>
CoroutineIterator<Ret> &CoroutineIterator<Ret>::operator++() {
  obtain_value();

  AIOXX_ASSUME(coro);

  holder.reset();

  return *this;
}

template<typename Ret>
bool CoroutineIterator<Ret>::operator==(const CoroutineIterator &other) const {
  obtain_value();
  other.obtain_value();

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
    return;
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
