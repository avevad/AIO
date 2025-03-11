#include "abi.hpp"

#ifdef AIO_SYSTEM_V_AMD64_ABI

    .intel_syntax
    .section .note.GNU-stack,"",@progbits


    .text
    .global aio_context_switch
aio_context_switch:
    // exchange all general-purpose registers
    xchg    rsp, [rdi+8]
    xchg    rbp, [rdi+16]
    xchg    rbx, [rdi+24]
    xchg    r12, [rdi+32]
    xchg    r13, [rdi+40]
    xchg    r14, [rdi+48]
    xchg    r15, [rdi+56]

    // load context rip into temporary
    mov     r11, [rdi]

    // set context rip to continuation point
    lea     r10, [rip+restore]
    mov     [rdi], r10

    // jump into context rip
    jmp     r11
restore:
    ret


    .text
    .global aio_context_trampoline
aio_context_trampoline:
    push    rbp

    // call context entrypoint
    call    rbx

    // return to parent context
    mov     rdi, rax
    call    aio_context_switch

    // abort on dead context
    call    aio_context_trap

#endif
