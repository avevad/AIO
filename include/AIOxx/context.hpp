#pragma once

#include "abi.hpp"

#include <cstddef>
#include <cstdint>

#if defined(__SANITIZE_ADDRESS__)
#define AIOXX_ASAN
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define AIOXX_ASAN
#endif
#endif

namespace AIO {

struct context_t;
using context_entrypoint_t = context_t &();

context_t make_context(context_entrypoint_t *entrypoint, void *stack, std::size_t stack_size);
void switch_to_context(context_t &ctx);
#ifdef AIOXX_ASAN
void destroy_context(context_t &ctx);
#else
inline void destroy_context(context_t &) {
}
#endif

#ifdef AIO_SYSTEM_V_AMD64_ABI

namespace _sysv_amd64 {

  extern "C" [[noreturn]] void aio_context_trampoline();
  extern "C" [[noreturn]] void aio_context_trap();

  extern "C" struct aio_context_t {
    std::uintptr_t rip = 0;
    std::uintptr_t rsp = 0;
  };

  static_assert(sizeof(aio_context_t) == 16);

  extern "C" context_t *aio_context_switch(context_t *ctx);

} // namespace _sysv_amd64

struct context_t {
  _sysv_amd64::aio_context_t _abi_ctx{};

#ifdef AIOXX_ASAN
  const void *_asan_stack_bottom = nullptr;
  std::size_t _asan_stack_size = 0;
  void *_asan_fake_stack = nullptr;
#endif
};

static_assert(offsetof(context_t, _abi_ctx) == 0);

#else
#error unsupported platform
#endif

} // namespace AIO
