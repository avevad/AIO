#include <gtest/gtest.h>

#include "AIOxx/context.hpp"

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
  context = AIO::make_context(run, stack.get(), CONTEXT_STACK_SIZE_BYTES);

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
