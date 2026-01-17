#pragma once

#include <iostream>
#include <optional>
#include <source_location>
#include <string>

#ifdef AIOXX_DEBUG
#define AIOXX_ASSUME(WHAT)                                                                                             \
  do {                                                                                                                 \
    if (!(WHAT)) {                                                                                                     \
      panic(std::string("assumption failed: ") + #WHAT);                                                               \
    }                                                                                                                  \
  } while (0)
#else
#define AIO_ASSUME(WHAT)                                                                                               \
  [[assume(WHAT)]]
#endif

#define AIOXX_UNREACHABLE AIOXX_ASSUME(false)

namespace AIO {

[[noreturn]] void panic(const std::string &what, std::source_location where = std::source_location::current());

[[noreturn]] void
panic(const std::string &what, const std::exception &e, std::source_location where = std::source_location::current());

void warning(const std::string &what, std::source_location where = std::source_location::current());

void warning(
  const std::string &what, const std::exception &e, std::source_location where = std::source_location::current()
);

template<typename Derived, typename Derived1>
class Bond {
public:
  Bond() = default;

  Bond(const Bond &) = delete;
  Bond &operator=(const Bond &) = delete;

  Bond(Bond &&other) noexcept : maybe_ptr(other.maybe_ptr) {
    other.maybe_ptr.reset();
    if (auto bound = get_base_ptr()) {
      bound->maybe_ptr = static_cast<Derived *>(this);
    }
  }

  Bond &operator=(Bond &&other) noexcept {
    if (&other == this) {
      return *this;
    }

    if (auto bound = get_base_ptr()) {
      bound->maybe_ptr = nullptr;
    }

    maybe_ptr = other.maybe_ptr;
    other.maybe_ptr.reset();
    if (auto bound = get_base_ptr()) {
      bound->maybe_ptr = static_cast<Derived *>(this);
    }

    return *this;
  }

  ~Bond() {
    if (auto bound = get_base_ptr()) {
      bound->maybe_ptr = nullptr;
    }
  }

protected:
  bool is_initialized() {
    return maybe_ptr.has_value();
  }

  bool is_bound() {
    AIOXX_ASSUME(is_initialized());
    return maybe_ptr != nullptr;
  }

  Derived1 *get_ptr() {
    AIOXX_ASSUME(is_initialized());
    return *maybe_ptr;
  }

  Derived1 &get() {
    AIOXX_ASSUME(is_bound());
    return **maybe_ptr;
  }

private:
  template<typename A, typename B>
    requires(std::derived_from<A, Bond<A, B>> && std::derived_from<B, Bond<B, A>>)
  friend void bind(A &a, B &b);

  friend class Bond<Derived1, Derived>;

  Bond<Derived1, Derived> *get_base_ptr() {
    return maybe_ptr.has_value() ? static_cast<Bond<Derived1, Derived> *>(*maybe_ptr) : nullptr;
  }

  std::optional<Derived1 *> maybe_ptr = std::nullopt;
};

template<typename A, typename B>
  requires(std::derived_from<A, Bond<A, B>> && std::derived_from<B, Bond<B, A>>)
void bind(A &a, B &b) {
  AIOXX_ASSUME(!a.is_initialized() && !b.is_initialized());
  a.maybe_ptr = &b;
  b.maybe_ptr = &a;
}
} // namespace AIO
