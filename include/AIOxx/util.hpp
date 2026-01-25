#pragma once

#include <exception>
#include <expected>
#include <iostream>
#include <optional>
#include <source_location>
#include <string>
#include <utility>

#ifdef AIOXX_DEBUG
#define AIOXX_ASSUME(WHAT)                                                                                             \
  do {                                                                                                                 \
    if (!(WHAT)) {                                                                                                     \
      AIO::panic(std::string("assumption failed: ") + #WHAT);                                                          \
    }                                                                                                                  \
  } while (0)
#else
#define AIOXX_ASSUME(WHAT) [[assume(WHAT)]]
#endif

#define AIOXX_UNREACHABLE                                                                                              \
  do {                                                                                                                 \
    AIOXX_ASSUME(false);                                                                                               \
    std::unreachable();                                                                                                \
  } while (0)

namespace AIO {

[[noreturn]] void panic(const std::string &what, std::source_location where = std::source_location::current());

[[noreturn]] void
panic(const std::string &what, std::exception_ptr err, std::source_location where = std::source_location::current());

void warning(const std::string &what, std::source_location where = std::source_location::current());

void warning(
  const std::string &what, std::exception_ptr err, std::source_location where = std::source_location::current()
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
  void initialize(Derived1 &bound) {
    AIOXX_ASSUME(!is_initialized());
    AIOXX_ASSUME(!bound.is_initialized());
    maybe_ptr = &bound;
    bound.maybe_ptr = static_cast<Derived *>(this);
  }

  bool is_initialized() const {
    return maybe_ptr.has_value();
  }

  bool is_alive() const {
    AIOXX_ASSUME(is_initialized());
    return maybe_ptr != nullptr;
  }

  Derived1 *get_ptr() const {
    AIOXX_ASSUME(is_initialized());
    return *maybe_ptr;
  }

  Derived1 &get() const {
    AIOXX_ASSUME(is_alive());
    return **maybe_ptr;
  }

private:
  friend class Bond<Derived1, Derived>;

  Bond<Derived1, Derived> *get_base_ptr() {
    return maybe_ptr.has_value() ? static_cast<Bond<Derived1, Derived> *>(*maybe_ptr) : nullptr;
  }

  std::optional<Derived1 *> maybe_ptr = std::nullopt;
};

template<typename Res>
using Expected = std::expected<Res, std::exception_ptr>;

template<typename Res>
struct ExpectedResult {
  using Result = Res;
  using Expected = Expected<Res>;

  template<typename Res1>
  using Mapped = ExpectedResult<Res1>;

  bool is_ok() const {
    return expected.has_value();
  }

  decltype(auto) move_as_ok() {
    AIOXX_ASSUME(is_ok());
    if constexpr (std::is_void_v<Res>) {
    } else {
      return std::move(*expected);
    }
  }

  auto move_as_err() {
    AIOXX_ASSUME(!is_ok());
    return std::move(expected.error());
  }

  template<typename R = Res>
  static ExpectedResult make_ok(R res)
    requires(!std::is_void_v<Res>)
  {
    return {.expected = std::move(res)};
  }

  static ExpectedResult make_ok()
    requires(std::is_void_v<Res>)
  {
    return {.expected = {}};
  }

  static ExpectedResult make_err(std::exception_ptr err) {
    AIOXX_ASSUME(err != nullptr);
    return {.expected = std::unexpected(std::move(err))};
  }

  static ExpectedResult make_err_from_current() {
    return make_err(std::current_exception());
  }

  template<typename Exception>
  static ExpectedResult make_err_from(const Exception &e) {
    try {
      throw e;
    } catch (...) {
      return make_err_from_current();
    }
  }

  Expected expected;
};

} // namespace AIO
