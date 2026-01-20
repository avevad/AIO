#include <gtest/gtest.h>

#include "AIOxx/coroutine.hpp"

namespace AIO {

} // namespace AIO

using namespace AIO;

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
