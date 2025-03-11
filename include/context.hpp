#pragma once

#include "abi.hpp"

#include <cstddef>
#include <cstdint>

namespace AIO {

    struct context_t;

    context_t make_context(void (*entrypoint)(), void *stack, std::size_t stack_size);
    void switch_to_context(context_t &ctx);

#ifdef AIO_SYSTEM_V_AMD64_ABI

    namespace _sysv_amd64 {

        extern "C" struct aio_context_t {
        private:
            struct alignas(8) R64 {
                union {
                    std::uint8_t bytes[8];
                    std::uint16_t words[4];
                    std::uint32_t d_words[2];
                    std::uint64_t q_word;
                };
            };

        public:
            R64 rip;

            R64 rsp;
            R64 rbp;

            R64 rbx;

            R64 r12;
            R64 r13;
            R64 r14;
            R64 r15;
        };
        static_assert(sizeof(aio_context_t) == 64);

        extern "C" void aio_context_trampoline();
        extern "C" void aio_context_create(aio_context_t *ctx, void *stack, std::size_t stack_size, void (*entrypoint)());
        extern "C" void aio_context_switch(aio_context_t *ctx);

    }

    struct context_t {
        _sysv_amd64::aio_context_t _abi_ctx;
    };

#else
#error unsupported platform
#endif

} // namespace AIO
