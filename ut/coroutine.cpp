#include <gtest/gtest.h>

#include "AIOxx/coroutine.hpp"

#include <vector>

using namespace AIO;

TEST(Coro, RetArg) {
  Coroutine<int(int)> *self = nullptr;
  std::vector<int> seen;

  Coroutine<int(int)> c([&](int x) {
    seen.push_back(x);
    x = self->yield(x + 1);
    seen.push_back(x);
    return x + 10;
  });
  self = &c;

  EXPECT_EQ(c.resume(1), 2);
  EXPECT_FALSE(c.is_dead());
  EXPECT_EQ(c.resume(5), 15);
  EXPECT_TRUE(c.is_dead());
  EXPECT_EQ(seen, (std::vector<int>{1, 5}));
}

TEST(Coro, VoidArg) {
  Coroutine<void(int)> *self = nullptr;
  int sum = 0;

  Coroutine<void(int)> c([&](int x) {
    sum += x;
    x = self->yield();
    sum += x;
  });
  self = &c;

  c.resume(2);
  EXPECT_FALSE(c.is_dead());
  c.resume(3);
  EXPECT_TRUE(c.is_dead());
  EXPECT_EQ(sum, 5);
}

TEST(Coro, RetVoid) {
  Coroutine<int()> c([] { return 5; });
  EXPECT_EQ(c.resume(), 5);
  EXPECT_TRUE(c.is_dead());
}

TEST(Coro, YieldAndKill) {
  Coroutine<void()> *self = nullptr;
  int step = 0;
  Coroutine<void()> c([&] {
    step = 1;
    self->yield();
    step = 2;
  });
  self = &c;

  c.resume();
  EXPECT_EQ(step, 1);
  EXPECT_FALSE(c.is_dead());

  c.kill();
  EXPECT_TRUE(c.is_dead());
}

TEST(Coro, ResumeTwice) {
  Coroutine<void()> *self = nullptr;
  int step = 0;
  Coroutine<void()> c([&] {
    step = 1;
    self->yield();
    step = 2;
  });
  self = &c;

  c.resume();
  EXPECT_EQ(step, 1);
  c.resume();
  EXPECT_EQ(step, 2);
  EXPECT_TRUE(c.is_dead());
}

TEST(Coro, ErrorPropagates) {
  Coroutine<int()> c([]() -> int { throw std::runtime_error("x"); });
  EXPECT_THROW((void) c.resume(), std::runtime_error);
  EXPECT_TRUE(c.is_dead());
}

TEST(Coro, Generator) {
  Coroutine<int()> *self = nullptr;
  Coroutine<int()> c([&] {
    self->yield(1);
    self->yield(2);
    throw EndGeneration();
    return 0;
  });
  self = &c;

  std::vector<int> got;
  for (int v : CoroutineGenerator<int>(c)) {
    got.push_back(v);
  }
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], 1);
  EXPECT_EQ(got[1], 2);

  // Iterator copy/assign (avoid driving the same coroutine twice).
  CoroutineIterator<int> end1 = CoroutineIteratorEnd{};
  CoroutineIterator<int> end2 = end1;
  end2 = end1;
  EXPECT_TRUE(end1 == end2);
}

TEST(Coro, EmptyGenerator) {
  CoroutineGenerator<int> gen;
  EXPECT_TRUE(gen.begin() == gen.end());
}
