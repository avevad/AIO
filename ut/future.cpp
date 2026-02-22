#include <gtest/gtest.h>

#include "AIOxx/future.hpp"

#include <string>

using namespace AIO;

namespace {

template<typename T>
Future<T> ok(T v) {
  auto [promise, future] = Contract<T>();
  std::move(promise).fulfill(std::move(v));
  return std::move(future);
}

Future<void> ok() {
  auto [promise, future] = Contract<void>();
  std::move(promise).fulfill();
  return std::move(future);
}

template<typename T>
Future<T> err(std::exception_ptr e) {
  auto [promise, future] = Contract<T>();
  std::move(promise).fail_any(std::move(e));
  return std::move(future);
}

} // namespace

TEST(Future, DefaultAndMove) {
  Future<int> f0;
  Promise<int> p0;
  (void) f0;
  (void) p0;

  auto [p, f] = Contract<int>();

  Future<int> f2;
  Promise<int> p2;
  f2 = std::move(f);
  p2 = std::move(p);

  bool called = false;
  auto f3 = std::move(f2).map_result([&](int x) {
    called = true;
    return x + 1;
  });
  std::move(p2).fulfill(1);
  std::move(f3).detach();
  EXPECT_TRUE(called);
}

TEST(Future, ConsumerBeforeResult) {
  auto [promise, future] = Contract<int>();
  int got = 0;
  auto f = std::move(future).map_result([](int x) { return x * 2; });
  auto f2 = std::move(f).map_expected([&](Expected<int> e) {
    got = e.value();
    return e;
  });
  std::move(promise).fulfill(3);
  std::move(f2).detach();
  EXPECT_EQ(got, 6);
}

TEST(Future, ResultBeforeConsumer) {
  auto [promise, future] = Contract<int>();
  std::move(promise).fulfill(4);

  int got = 0;
  auto f = std::move(future).map_result([](int x) { return x + 1; });
  auto f2 = std::move(f).map_expected([&](Expected<int> e) {
    got = e.value();
    return e;
  });
  std::move(f2).detach();
  EXPECT_EQ(got, 5);
}

TEST(Future, MapToVoid) {
  auto [promise, future] = Contract<int>();
  bool called = false;
  auto f = std::move(future).map_result([&](int) { called = true; });
  std::move(promise).fulfill(1);
  std::move(f).detach();
  EXPECT_TRUE(called);
}

TEST(Future, MapExpectedToVoid) {
  auto [promise, future] = Contract<int>();
  bool called = false;
  auto f = std::move(future).map_expected([&](Expected<int>) -> std::expected<void, std::exception_ptr> {
    called = true;
    return {};
  });
  std::move(promise).fulfill(1);
  std::move(f).detach();
  EXPECT_TRUE(called);
}

TEST(Future, ThenNonVoid) {
  auto [promise, future] = Contract<int>();
  int got = 0;
  auto f = std::move(future).then([](int x) { return ok(x + 1); });
  auto f2 = std::move(f).map_result([&](int x) {
    got = x;
    return x;
  });
  std::move(promise).fulfill(5);
  std::move(f2).detach();
  EXPECT_EQ(got, 6);
}

TEST(Future, ThenVoid) {
  auto [promise, future] = Contract<int>();
  bool called = false;
  auto f = std::move(future).then([&](int) {
    called = true;
    return ok();
  });
  std::move(promise).fulfill(1);
  std::move(f).detach();
  EXPECT_TRUE(called);
}

TEST(Future, VoidThenAndMapExpected) {
  auto [promise, future] = Contract<void>();
  bool then_called = false;
  int got = 0;

  // then: void -> int
  auto f = std::move(future).then([&] {
    then_called = true;
    return ok(4);
  });
  auto f2 = std::move(f).map_result([&](int x) {
    got = x;
    return x;
  });
  std::move(promise).fulfill();
  std::move(f2).detach();
  EXPECT_TRUE(then_called);
  EXPECT_EQ(got, 4);

  // map_expected: void -> void
  bool me_called = false;
  auto f3 = ok().map_expected([&](Expected<void> e) -> std::expected<void, std::exception_ptr> {
    me_called = true;
    if (!e)
      return std::unexpected(e.error());
    return {};
  });
  std::move(f3).detach();
  EXPECT_TRUE(me_called);

  // map_expected: void -> int
  int me_got = 0;
  auto f4 = ok().map_expected([](Expected<void> e) -> std::expected<int, std::exception_ptr> {
    if (!e)
      return std::unexpected(e.error());
    return 5;
  });
  auto f5 = std::move(f4).map_result([&](int x) {
    me_got = x;
    return x;
  });
  std::move(f5).detach();
  EXPECT_EQ(me_got, 5);
}

TEST(Future, ThenOnError) {
  bool called = false;
  auto f = err<int>(std::make_exception_ptr(std::runtime_error("x"))).then([&](int) {
    called = true;
    return ok(1);
  });
  std::move(f).detach();
  EXPECT_FALSE(called);
}

TEST(Future, ExceptTyped) {
  auto [promise, future] = Contract<int>();
  bool handled = false;
  int got = 0;

  auto f = std::move(future).except<std::runtime_error>([&](std::runtime_error &) {
    handled = true;
    return ok(9);
  });
  auto f2 = std::move(f).map_result([&](int x) {
    got = x;
    return x;
  });
  std::move(promise).fail(std::runtime_error("x"));
  std::move(f2).detach();
  EXPECT_TRUE(handled);
  EXPECT_EQ(got, 9);
}

TEST(Future, ExceptAny) {
  auto [promise, future] = Contract<int>();
  bool typed = false;
  bool any = false;
  int got = 0;

  auto f = std::move(future)
             .except<std::runtime_error>([&](std::runtime_error &) {
               typed = true;
               return ok(1);
             })
             .except_any([&](std::exception_ptr) {
               any = true;
               return ok(2);
             });

  auto f2 = std::move(f).map_result([&](int x) {
    got = x;
    return x;
  });
  std::move(promise).fail(std::logic_error("y"));
  std::move(f2).detach();

  EXPECT_FALSE(typed);
  EXPECT_TRUE(any);
  EXPECT_EQ(got, 2);
}

TEST(Future, FunctorThrows) {
  auto [promise, future] = Contract<int>();
  bool handled = false;

  auto f = std::move(future).map_result(
                              [](int) -> int { throw std::runtime_error("boom"); }
  ).except_any([&](std::exception_ptr) {
    handled = true;
    return ok(7);
  });
  std::move(promise).fulfill(1);
  std::move(f).detach();
  EXPECT_TRUE(handled);
}

TEST(Future, DetachOnError) {
  auto f = err<int>(std::make_exception_ptr(std::runtime_error("x")));
  std::move(f).detach();
}

TEST(Future, ErrorShortCircuits) {
  bool called = false;
  auto f = err<int>(std::make_exception_ptr(std::runtime_error("x")))
             .then([&](int) {
               called = true;
               return ok(1);
             })
             .map_result([&](int) {
               called = true;
               return 2;
             });
  std::move(f).detach();
  EXPECT_FALSE(called);
}

TEST(Future, ExceptAnyHandlerThrows) {
  auto f =
    err<void>(std::make_exception_ptr(std::runtime_error("x"))).except_any([](std::exception_ptr) -> Future<void> {
      throw std::runtime_error("handler");
    });
  std::move(f).detach();
}

TEST(Future, CancelHangup) {
  auto [promise, future] = Contract<int>();
  bool hangup = false;
  promise.set_hangup_handler([&] { hangup = true; });

  std::move(future).cancel();
  EXPECT_TRUE(hangup);
}

TEST(Future, CancelLateHangup) {
  auto [promise, future] = Contract<int>();
  std::move(future).cancel();

  bool hangup = false;
  promise.set_hangup_handler([&] { hangup = true; });
  EXPECT_TRUE(hangup);
}

TEST(Future, CancelPending) {
  auto [promise, future] = Contract<int>();
  bool called = false;
  auto f = std::move(future).map_result([&](int) {
    called = true;
    return 1;
  });

  std::move(f).cancel();
  std::move(promise).fulfill(7);
  EXPECT_FALSE(called);
}

TEST(Future, CancelMap) {
  auto [promise, future] = Contract<int>();
  bool hangup = false;
  promise.set_hangup_handler([&] { hangup = true; });

  auto f = std::move(future).map_result([](int x) { return x + 1; });
  std::move(f).cancel();
  EXPECT_TRUE(hangup);
}

TEST(Future, CancelChainOk) {
  auto [promise, future] = Contract<int>();
  auto [nested_promise, nested_future] = Contract<int>();
  bool map_called = false;
  bool then_called = false;
  bool typed_called = false;
  bool any_called = false;
  bool tail_called = false;
  bool nested_hangup = false;
  nested_promise.set_hangup_handler([&] { nested_hangup = true; });

  auto f =
    std::move(future)
      .map_expected([&](Expected<int> e) {
        map_called = true;
        return e;
      })
      .then([&](int) {
        then_called = true;
        return std::move(nested_future);
      })
      .except<std::runtime_error>([&](std::runtime_error &) {
        typed_called = true;
        return ok(9);
      })
      .except_any([&](std::exception_ptr) {
        any_called = true;
        return ok(10);
      })
      .map_result([&](int x) {
        tail_called = true;
        return x + 1;
      });

  std::move(promise).fulfill(1);
  std::move(f).cancel();

  EXPECT_TRUE(map_called);
  EXPECT_TRUE(then_called);
  EXPECT_FALSE(typed_called);
  EXPECT_FALSE(any_called);
  EXPECT_FALSE(tail_called);
  EXPECT_TRUE(nested_hangup);
}

TEST(Future, CancelChainErr) {
  auto [promise, future] = Contract<int>();
  auto [nested_promise, nested_future] = Contract<int>();
  bool then_called = false;
  bool typed_called = false;
  bool any_called = false;
  bool tail_called = false;
  bool nested_hangup = false;
  nested_promise.set_hangup_handler([&] { nested_hangup = true; });

  auto f =
    std::move(future)
      .then([&](int) {
        then_called = true;
        return ok(1);
      })
      .except<std::logic_error>([&](std::logic_error &) {
        typed_called = true;
        return ok(2);
      })
      .except_any([&](std::exception_ptr) {
        any_called = true;
        return std::move(nested_future);
      })
      .map_result([&](int x) {
        tail_called = true;
        return x + 1;
      });

  std::move(promise).fail(std::runtime_error("x"));
  std::move(f).cancel();

  EXPECT_FALSE(then_called);
  EXPECT_FALSE(typed_called);
  EXPECT_TRUE(any_called);
  EXPECT_FALSE(tail_called);
  EXPECT_TRUE(nested_hangup);
}

TEST(Future, DetachedCancel) {
  auto [promise, future] = Contract<int>();
  bool hangup = false;
  promise.set_hangup_handler([&] { hangup = true; });

  DetachedFuture<int> detached(std::move(future));
  std::move(detached).cancel();
  EXPECT_TRUE(hangup);
}

TEST(Future, ConsumedCancel) {
  auto [promise, future] = Contract<int>();
  bool hangup = false;
  promise.set_hangup_handler([&] { hangup = true; });

  ConsumedFuture<int> consumed(std::move(future));
  std::move(consumed).cancel();
  EXPECT_TRUE(hangup);
}

TEST(Future, ConsumedAutoCancel) {
  auto [promise, future] = Contract<int>();
  bool hangup = false;
  promise.set_hangup_handler([&] { hangup = true; });

  { ConsumedFuture<int> consumed(std::move(future)); }
  EXPECT_TRUE(hangup);
}

TEST(Future, ConsumedDetached) {
  auto [promise, future] = Contract<int>();
  bool hangup = false;
  promise.set_hangup_handler([&] { hangup = true; });

  auto consumed = ConsumedFuture<int>(std::move(future));
  auto detached = std::move(consumed).release();
  EXPECT_FALSE(hangup);

  { auto consumed1 = std::move(detached).hold(); }
  EXPECT_TRUE(hangup);
}