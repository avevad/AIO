#include "context.hpp"

namespace AIO {

#ifdef AIO_SYSTEM_V_AMD64_ABI

    context_t make_context(void (*entrypoint)(), void *stack, std::size_t stack_size) {
        // calculate stack bottom and align by 16 bytes
        auto *stack_bytes = static_cast<char *>(stack);
        auto stack_bottom = reinterpret_cast<uintptr_t>(stack_bytes + stack_size);
        stack_bottom &= ~0xfull;

        // store return address stub
        stack_bottom -= sizeof(void *);
        *reinterpret_cast<void **>(stack_bottom) = nullptr;

        context_t ctx {};
        ctx._abi_ctx.rip.q_word = reinterpret_cast<uintptr_t>(_sysv_amd64::aio_context_trampoline);
        ctx._abi_ctx.rsp.q_word = stack_bottom;
        ctx._abi_ctx.rbp.q_word = 0;
        ctx._abi_ctx.rbx.q_word = reinterpret_cast<uintptr_t>(entrypoint); // used in the trampoline

        return ctx;
    }

    void switch_to_context(context_t &ctx) {
        aio_context_switch(&ctx._abi_ctx);
    }


#endif

} // namespace AIO
