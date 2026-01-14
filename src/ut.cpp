#include <gtest/gtest.h>

#include "AIOxx/context.hpp"
#include "AIOxx/coroutine.hpp"

using namespace AIO;

constexpr static std::size_t CONTEXT_STACK_SIZE_BYTES = 16 * 1024; // 16 KiB

auto make_context_stack(std::size_t size = CONTEXT_STACK_SIZE_BYTES) {
  return std::make_unique<char[]>(size);
}


TEST(Contexts, Running) {
  static context_t context{};
  static volatile bool flag = false;

  auto run = [] -> context_t & {
    flag = true;
    return context;
  };
  auto stack = make_context_stack();
  context = make_context(run, stack.get(), CONTEXT_STACK_SIZE_BYTES);

  ASSERT_EQ(flag, false);
  switch_to_context(context);
  ASSERT_EQ(flag, true);
}

TEST(Contexts, Switching) {
  static context_t context{};
  static volatile int step = 0;

  auto run = [] -> context_t & {
    while (step != 100) {
      step = step + 1;
      switch_to_context(context);
    }
    step = -1;
    return context;
  };
  auto stack = make_context_stack();
  context = make_context(run, stack.get(), CONTEXT_STACK_SIZE_BYTES);

  for (int step1 = 0; step1 != 100; step1++) {
    ASSERT_EQ(step, step1);
    switch_to_context(context);
  }

  ASSERT_EQ(step, 100);
  switch_to_context(context);
  ASSERT_EQ(step, -1);
}

TEST(Contexts, Multiple) {
  static context_t context1{};
  static volatile int step1 = 0;
  auto run1 = [] -> context_t & {
    while (true) {
      step1 = step1 + 1;
      if (step1 == 50) {
        break;
      }
      switch_to_context(context1);
    }
    return context1;
  };
  auto stack1 = make_context_stack();
  context1 = make_context(run1, stack1.get(), CONTEXT_STACK_SIZE_BYTES);

  static context_t context2{};
  static volatile int step2 = 100;
  auto run2 = [] -> context_t & {
    while (true) {
      step2 = step2 - 1;
      if (step2 == 50) {
        break;
      }
      switch_to_context(context2);
    }
    return context2;
  };
  auto stack2 = make_context_stack();
  context2 = make_context(run2, stack2.get(), CONTEXT_STACK_SIZE_BYTES);

  while (step1 != step2) {
    switch_to_context(context1);
    switch_to_context(context2);
  }

  ASSERT_EQ(step1, 50);
}

class InstanceCounter {
public:
  InstanceCounter() {
    counter++;
  }

  InstanceCounter(const InstanceCounter &) = delete;
  InstanceCounter(InstanceCounter &&) = delete;

  InstanceCounter &operator=(const InstanceCounter &) = delete;
  InstanceCounter &operator=(InstanceCounter &&) = delete;

  static std::size_t count() {
    return counter;
  }

  ~InstanceCounter() {
    counter--;
  }

private:
  static std::size_t counter;
};

std::size_t InstanceCounter::counter = 0;

class Coroutines : public testing::Test {
protected:
  constexpr static int FIB_MAX = 100000;

  Coroutine<int()> fib = [this] -> int {
    for (int prev = 0, cur = 1;;) {
      if (cur > FIB_MAX)
        throw EndGeneration();

      fib.yield(cur);

      const int next = prev + cur;
      prev = cur;
      cur = next;
    }
  };

  volatile int step = 0;
  Coroutine<void()> stepper = [this] {
    InstanceCounter counter;
    try {
      while (true) {
        step = step + 1;
        if (step == 100) {
          return;
        }
        stepper.yield();
      }
    } catch (...) {
      // this guard will be executed, but the coroutine will be killed nevertheless
      step = -1;
    }
  };

  void TearDown() override {
    if (!fib.is_dead()) {
      fib.kill();
    }
    if (!stepper.is_dead()) {
      stepper.kill();
    }
  }
};

TEST_F(Coroutines, RunningUntilDead) {
  while (!stepper.is_dead()) {
    ASSERT_NE(step, 100);
    stepper.resume();
  }
  ASSERT_EQ(step, 100);
}

TEST_F(Coroutines, Killing) {
  ASSERT_EQ(InstanceCounter::count(), 0);

  stepper.resume();
  ASSERT_EQ(InstanceCounter::count(), 1);

  stepper.resume();
  ASSERT_EQ(InstanceCounter::count(), 1);

  stepper.kill();
  ASSERT_EQ(InstanceCounter::count(), 0);
  ASSERT_EQ(stepper.is_dead(), true);
  ASSERT_EQ(step, -1);
}

TEST_F(Coroutines, Iteration) {
  CoroutineGenerator fib_generator(fib);

  std::array<int, 5> fibs{}, fibs_exp{1, 1, 2, 3, 5};
  std::copy_n(fib_generator.begin(), 5, fibs.begin());
  ASSERT_EQ(fibs, fibs_exp);

  int last_fib = fibs.back();

  for (auto e : fib_generator) {
    ASSERT_GT(e, last_fib);
    ASSERT_LE(e, FIB_MAX);
    last_fib = e;
  }

  ASSERT_EQ(last_fib, 75025);
}
