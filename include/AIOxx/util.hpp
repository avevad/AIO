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
  const std::string &what,
  std::exception_ptr err,
  std::source_location where = std::source_location::current()
);

template<typename Derived, typename Derived1, bool Master>
class Bond {
public:
  Bond() = default;

  Bond(const Bond &) = delete;
  Bond &operator=(const Bond &) = delete;

  Bond(Bond &&other) noexcept;
  Bond &operator=(Bond &&other) noexcept;

  ~Bond();

protected:
  void initialize(Derived1 &bound);

  bool is_initialized() const;
  bool is_alive() const;

  Derived1 *get_ptr() const;
  Derived1 &get() const;

private:
  friend class Bond<Derived1, Derived, !Master>;

  Bond<Derived1, Derived, !Master> *get_base_ptr();

  std::optional<Derived1 *> maybe_ptr = std::nullopt;
};

template<typename T>
class BoundStorageSlave;

template<typename T>
class BoundStorageMaster : public Bond<BoundStorageMaster<T>, BoundStorageSlave<T>, true> {
  using Bond = Bond<BoundStorageMaster, BoundStorageSlave<T>, true>;

public:
  explicit BoundStorageMaster(T value);

  BoundStorageMaster(BoundStorageMaster &&) noexcept = default;
  BoundStorageMaster &operator=(BoundStorageMaster &&other) noexcept;

  BoundStorageMaster(const BoundStorageMaster &) = delete;
  BoundStorageMaster &operator=(const BoundStorageMaster &) = delete;

  ~BoundStorageMaster();

  void bind_to(BoundStorageSlave<T> &bound);

  T &value();
  const T &value() const;

private:
  T stored_value;
};

template<typename T>
class BoundStorageSlave : public Bond<BoundStorageSlave<T>, BoundStorageMaster<T>, false> {
  using Bond = Bond<BoundStorageSlave, BoundStorageMaster<T>, false>;

public:
  BoundStorageSlave() = default;

  BoundStorageSlave(BoundStorageSlave &&) noexcept = default;
  BoundStorageSlave &operator=(BoundStorageSlave &&) noexcept = default;

  BoundStorageSlave(const BoundStorageSlave &) = delete;
  BoundStorageSlave &operator=(const BoundStorageSlave &) = delete;

  void bind_to(BoundStorageMaster<T> &bound);

  T &value();
  const T &value() const;

private:
  friend class BoundStorageMaster<T>;

  std::optional<T> maybe_value = std::nullopt;
};

template<typename T>
[[nodiscard]] auto make_storage(T value) {
  struct Storage {
    BoundStorageMaster<T> master;
    BoundStorageSlave<T> slave = {};

    explicit Storage(T value) : master(std::move(value)) {
      master.bind_to(slave);
    }
  };

  return Storage{std::move(value)};
}

template<typename Res>
using Expected = std::expected<Res, std::exception_ptr>;

template<typename Res>
struct ExpectedResult {
  using Result = Res;
  using Expected = Expected<Res>;
  template<typename Res1>
  using Mapped = ExpectedResult<Res1>;

  bool is_ok() const;

  decltype(auto) move_as_ok();
  auto move_as_err();

  template<typename R = Res>
  static ExpectedResult make_ok(R res)
    requires(!std::is_void_v<Res>);
  static ExpectedResult make_ok()
    requires(std::is_void_v<Res>);

  template<typename Exception>
  static ExpectedResult make_err_from(const Exception &e);
  static ExpectedResult make_err(std::exception_ptr err);
  static ExpectedResult make_err_from_current();

  Expected expected;
};

} // namespace AIO


// --------------------------------------------------
// -------------- TEMPLATE DEFINITIONS --------------
// --------------------------------------------------


namespace AIO {
template<typename Derived, typename Derived1, bool Master>
Bond<Derived, Derived1, Master>::Bond(Bond &&other) noexcept : maybe_ptr(other.maybe_ptr) {
  other.maybe_ptr.reset();
  if (auto bound = get_base_ptr()) {
    bound->maybe_ptr = static_cast<Derived *>(this);
  }
}

template<typename Derived, typename Derived1, bool Master>
Bond<Derived, Derived1, Master> &Bond<Derived, Derived1, Master>::operator=(Bond &&other) noexcept {
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

template<typename Derived, typename Derived1, bool Master>
Bond<Derived, Derived1, Master>::~Bond() {
  if (auto bound = get_base_ptr()) {
    bound->maybe_ptr = nullptr;
  }
}

template<typename Derived, typename Derived1, bool Master>
void Bond<Derived, Derived1, Master>::initialize(Derived1 &bound) {
  AIOXX_ASSUME(!is_initialized());
  AIOXX_ASSUME(!bound.is_initialized());
  maybe_ptr = &bound;
  bound.maybe_ptr = static_cast<Derived *>(this);
}

template<typename Derived, typename Derived1, bool Master>
bool Bond<Derived, Derived1, Master>::is_initialized() const {
  return maybe_ptr.has_value();
}

template<typename Derived, typename Derived1, bool Master>
bool Bond<Derived, Derived1, Master>::is_alive() const {
  AIOXX_ASSUME(is_initialized());
  return maybe_ptr != nullptr;
}

template<typename Derived, typename Derived1, bool Master>
Derived1 *Bond<Derived, Derived1, Master>::get_ptr() const {
  AIOXX_ASSUME(is_initialized());
  return *maybe_ptr;
}

template<typename Derived, typename Derived1, bool Master>
Derived1 &Bond<Derived, Derived1, Master>::get() const {
  AIOXX_ASSUME(is_alive());
  return **maybe_ptr;
}

template<typename Derived, typename Derived1, bool Master>
Bond<Derived1, Derived, !Master> *Bond<Derived, Derived1, Master>::get_base_ptr() {
  return maybe_ptr.has_value() ? static_cast<Bond<Derived1, Derived, !Master> *>(*maybe_ptr) : nullptr;
}

template<typename T>
BoundStorageMaster<T>::BoundStorageMaster(T value) : stored_value(std::move(value)) {
}

template<typename T>
BoundStorageMaster<T> &BoundStorageMaster<T>::operator=(BoundStorageMaster &&other) noexcept {
  if (this == &other)
    return *this;

  if (Bond::is_initialized() && Bond::is_alive())
    Bond::get().maybe_value.emplace(std::move(stored_value));

  Bond::operator=(std::move(other));
  stored_value = std::move(other.stored_value);
  return *this;
}

template<typename T>
BoundStorageMaster<T>::~BoundStorageMaster() {
  if (Bond::is_initialized() && Bond::is_alive())
    Bond::get().maybe_value.emplace(std::move(stored_value));
}

template<typename T>
void BoundStorageMaster<T>::bind_to(BoundStorageSlave<T> &bound) {
  AIOXX_ASSUME(!bound.maybe_value.has_value());
  Bond::initialize(bound);
}

template<typename T>
T &BoundStorageMaster<T>::value() {
  return stored_value;
}

template<typename T>
const T &BoundStorageMaster<T>::value() const {
  return stored_value;
}

template<typename T>
void BoundStorageSlave<T>::bind_to(BoundStorageMaster<T> &bound) {
  bound.bind_to(*this);
}

template<typename T>
T &BoundStorageSlave<T>::value() {
  if (Bond::is_alive())
    return Bond::get().value();
  return *maybe_value;
}

template<typename T>
const T &BoundStorageSlave<T>::value() const {
  return const_cast<BoundStorageSlave *>(this)->value();
}

template<typename Res>
bool ExpectedResult<Res>::is_ok() const {
  return expected.has_value();
}

template<typename Res>
decltype(auto) ExpectedResult<Res>::move_as_ok() {
  AIOXX_ASSUME(is_ok());
  if constexpr (std::is_void_v<Res>) {
  } else {
    return std::move(*expected);
  }
}

template<typename Res>
auto ExpectedResult<Res>::move_as_err() {
  AIOXX_ASSUME(!is_ok());
  return std::move(expected.error());
}

template<typename Res>
template<typename R>
ExpectedResult<Res> ExpectedResult<Res>::make_ok(R res)
  requires(!std::is_void_v<Res>)
{
  return {.expected = std::move(res)};
}

template<typename Res>
ExpectedResult<Res> ExpectedResult<Res>::make_ok()
  requires(std::is_void_v<Res>)
{
  return {.expected = {}};
}

template<typename Res>
ExpectedResult<Res> ExpectedResult<Res>::make_err(std::exception_ptr err) {
  AIOXX_ASSUME(err != nullptr);
  return {.expected = std::unexpected(std::move(err))};
}

template<typename Res>
ExpectedResult<Res> ExpectedResult<Res>::make_err_from_current() {
  return make_err(std::current_exception());
}

template<typename Res>
template<typename Exception>
ExpectedResult<Res> ExpectedResult<Res>::make_err_from(const Exception &e) {
  try {
    throw e;
  } catch (...) {
    return make_err_from_current();
  }
}
} // namespace AIO
