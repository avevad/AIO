#ifndef CONTEXT_H
#define CONTEXT_H

#include "abi.hpp"

#include <cstddef>

namespace AIO {
    extern "C" struct aio_context;

    extern "C" void aio_context_switch(aio_context *ctx);

    extern "C" void aio_context_create(aio_context *ctx, void *stack, std::size_t stack_size, void (*entrypoint)());

#ifdef AIO_SYSTEM_V_AMD64_ABI
    struct aio_context {
    private:
        struct alignas(8) R64 {
            char reg[8];
        };

        struct alignas(16) XMM {
            char reg[16];
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
    static_assert(sizeof(aio_context) == 64);
#else
#error unsupported platform
#endif
}

#endif //CONTEXT_H
