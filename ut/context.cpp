#include <gtest/gtest.h>

#include "AIOxx/context.hpp"
#include "AIOxx/util.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

using namespace AIO;

namespace {

static context_t *g_ctx = nullptr;
static int g_hits = 0;

context_t &ctx_entry() {
  ++g_hits;
  return *g_ctx;
}

} // namespace

TEST(Context, Make) {
  alignas(16) std::array<std::byte, 4096> stack{};

  context_t ctx = make_context(&ctx_entry, stack.data(), stack.size());

  // ABI/stack properties of the created context.
  EXPECT_EQ(ctx._abi_ctx.rip.q_word, reinterpret_cast<std::uintptr_t>(&AIO::_sysv_amd64::aio_context_trampoline));
  EXPECT_EQ(ctx._abi_ctx.rsp.q_word % 16u, 8u);
  EXPECT_EQ(ctx._abi_ctx.rbx.q_word, reinterpret_cast<std::uintptr_t>(&ctx_entry));
}

TEST(Context, Switch) {
  alignas(16) std::array<std::byte, 4096> stack{};
  context_t ctx = make_context(&ctx_entry, stack.data(), stack.size());
  g_ctx = &ctx;
  g_hits = 0;
  auto initial_rip = ctx._abi_ctx.rip.q_word;

  switch_to_context(ctx);

  EXPECT_EQ(g_hits, 1);
  EXPECT_NE(ctx._abi_ctx.rip.q_word, initial_rip); // switched-out continuation installed
}

TEST(Util, WarningWithExceptions) {
  warning("x");
  warning("x", std::make_exception_ptr(std::runtime_error("boom")));
  warning("x", std::make_exception_ptr(123));
}

TEST(Util, PanicDies) {
  EXPECT_DEATH({ panic("boom"); }, ".*");
}

#ifdef AIOXX_DEBUG
TEST(Context, TrapDies) {
  EXPECT_DEATH({ AIO::_sysv_amd64::aio_context_trap(); }, ".*");
}
#endif
