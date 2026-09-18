#include "AIOxx/context.hpp"

#include "AIOxx/util.hpp"

#ifdef AIOXX_ASAN
#include <sanitizer/common_interface_defs.h>
#endif

namespace AIO {

#ifdef AIO_SYSTEM_V_AMD64_ABI

context_t make_context(context_entrypoint_t *entrypoint, void *stack, std::size_t stack_size) {
  // calculate stack bottom and align by 16 bytes
  auto *stack_bytes = static_cast<char *>(stack);
  auto stack_bottom = reinterpret_cast<uintptr_t>(stack_bytes + stack_size);
  stack_bottom &= ~0xfull;

  // store entrypoint for the trampoline
  stack_bottom -= sizeof(entrypoint);
  *reinterpret_cast<context_entrypoint_t **>(stack_bottom) = entrypoint;

  context_t ctx{};
  ctx._abi_ctx.rip = reinterpret_cast<uintptr_t>(_sysv_amd64::aio_context_trampoline);
  ctx._abi_ctx.rsp = stack_bottom;
#ifdef AIOXX_ASAN
  ctx._asan_stack_bottom = stack;
  ctx._asan_stack_size = stack_size;
#endif

  return ctx;
}

#ifdef AIOXX_ASAN
__attribute__((no_sanitize_address))
#endif
void switch_to_context(context_t &ctx) {
#ifdef AIOXX_ASAN
  void *fake_stack = nullptr;
  __sanitizer_start_switch_fiber(&fake_stack, ctx._asan_stack_bottom, ctx._asan_stack_size);
  ctx._asan_fake_stack = fake_stack;
#endif

  [[maybe_unused]] auto *from = _sysv_amd64::aio_context_switch(&ctx);

#ifdef AIOXX_ASAN
  __sanitizer_finish_switch_fiber(fake_stack, &from->_asan_stack_bottom, &from->_asan_stack_size);
#endif
}

extern "C"
#ifdef AIOXX_ASAN
  __attribute__((no_sanitize_address))
#endif
  [[noreturn]] void aio_context_entry([[maybe_unused]] context_t *from, context_entrypoint_t *entrypoint) {
#ifdef AIOXX_ASAN
  __sanitizer_finish_switch_fiber(nullptr, &from->_asan_stack_bottom, &from->_asan_stack_size);
#endif

  auto &next = entrypoint();

#ifdef AIOXX_ASAN
  __sanitizer_start_switch_fiber(nullptr, next._asan_stack_bottom, next._asan_stack_size);
  next._asan_fake_stack = nullptr;
#endif
  _sysv_amd64::aio_context_switch(&next);
  _sysv_amd64::aio_context_trap();
}

#ifdef AIOXX_ASAN
__attribute__((no_sanitize_address)) void destroy_context(context_t &ctx) {
  if (!ctx._asan_fake_stack)
    return;

  // ASan can only destroy the selected fake stack. Select the suspended one
  // without running coroutine code, then restore the running stack's metadata.
  void *fake_stack = nullptr;
  const void *stack_bottom = nullptr;
  std::size_t stack_size = 0;
  __sanitizer_start_switch_fiber(&fake_stack, ctx._asan_stack_bottom, ctx._asan_stack_size);
  __sanitizer_finish_switch_fiber(ctx._asan_fake_stack, &stack_bottom, &stack_size);
  __sanitizer_start_switch_fiber(nullptr, stack_bottom, stack_size);
  __sanitizer_finish_switch_fiber(fake_stack, nullptr, nullptr);
  ctx._asan_fake_stack = nullptr;
}
#endif

void _sysv_amd64::aio_context_trap() {
  AIOXX_UNREACHABLE;
}


#endif

} // namespace AIO
