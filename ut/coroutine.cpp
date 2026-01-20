#include <gtest/gtest.h>

#include "AIOxx/coroutine.hpp"

using namespace AIO;

// Use this to check lifetime of objects
class InstanceCounter {
public:
  using Ptr = std::unique_ptr<InstanceCounter>;

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

TEST(Coroutines, RunningUntilDead) {
}

TEST(Coroutines, Killing) {
}

TEST(Coroutines, Iteration) {
}
