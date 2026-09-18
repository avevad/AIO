#include "AIOxx/abi.hpp"

#ifdef AIO_SYSTEM_V_AMD64_ABI

    .intel_syntax noprefix
    .section .note.GNU-stack,"",@progbits


    .text
    .global aio_context_switch
aio_context_switch:
    // save callee-saved registers on the current stack
    push    rbp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15

    // exchange stacks
    xchg    rsp, [rdi+8]

    // load context rip into temporary
    mov     r11, [rdi]

    // set context rip to continuation point
    lea     r10, [rip+restore]
    mov     [rdi], r10

    // return the context holding the outgoing stack to the resumed caller
    mov     rax, rdi

    // jump into context rip
    jmp     r11
restore:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret


    .text
    .global aio_context_trampoline
aio_context_trampoline:
    // load entrypoint, align the stack for calls and end the frame chain
    pop     rsi
    xor     ebp, ebp

    // finish the initial switch before calling instrumented code
    call    aio_context_entry

#endif
