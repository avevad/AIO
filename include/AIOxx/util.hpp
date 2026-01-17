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
class Bound {
public:
  Bound() = default;

  Bound(const Bound &) = delete;
  Bound &operator=(const Bound &) = delete;

  Bound(Bound &&other) noexcept : bond(other.bond) {
    other.bond.reset();
    if (auto bound = get_bound_base_ptr()) {
      bound->bond = static_cast<Derived *>(this);
    }
  }

  Bound &operator=(Bound &&other) noexcept {
    if (&other == this) {
      return *this;
    }

    if (auto bound = get_bound_base_ptr()) {
      bound->bond = nullptr;
    }

    bond = other.bond;
    other.bond.reset();
    if (auto bound = get_bound_base_ptr()) {
      bound->bond = static_cast<Derived *>(this);
    }

    return *this;
  }

  ~Bound() {
    if (auto bound = get_bound_base_ptr()) {
      bound->bond = nullptr;
    }
  }

protected:
  bool is_bound() {
    return bond.has_value() && bond.value();
  }

  bool was_bound() {
    return bond.has_value();
  }

  Derived1 *get_bound_ptr() {
    AIOXX_ASSUME(is_bound());
    return bond.value();
  }

  Derived1 &get_bound_obj() {
    AIOXX_ASSUME(is_bound());
    return *bond.value();
  }

private:
  template<typename A, typename B>
    requires(std::derived_from<A, Bound<A, B>> && std::derived_from<B, Bound<B, A>>)
  friend void bind(A &a, B &b);

  friend class Bound<Derived1, Derived>;

  Bound<Derived1, Derived> *get_bound_base_ptr() {
    return bond.has_value() ? static_cast<Bound<Derived1, Derived> *>(bond.value()) : nullptr;
  }

  std::optional<Derived1 *> bond = std::nullopt;
};

template<typename A, typename B>
  requires(std::derived_from<A, Bound<A, B>> && std::derived_from<B, Bound<B, A>>)
void bind(A &a, B &b) {
  AIOXX_ASSUME(!a.bond.has_value() && !b.bond.has_value());
  a.bond = &b;
  b.bond = &a;
}
} // namespace AIO
