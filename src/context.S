    .intel_syntax

#include "abi.hpp"

#ifdef AIO_SYSTEM_V_AMD64_ABI

    .section .note.GNU-stack,"",@progbits

    .text
    .global aio_context_switch
aio_context_switch:
    xchg    rsp, [rdi+8]
    xchg    rbp, [rdi+16]
    xchg    rbx, [rdi+24]
    xchg    r12, [rdi+32]
    xchg    r13, [rdi+40]
    xchg    r14, [rdi+48]
    xchg    r15, [rdi+56]                               // exchange all general-purpose registers

    mov     r11, [rdi]                                  // load context rip into temporary

    lea     r10, [rip+restore]
    mov     [rdi], r10                                  // set context rip to continuation point

    jmp     r11                                         // jump into context rip
restore:
    ret

    .text
    .global aio_context_trampoline
aio_context_trampoline:
    push    rbp

    call    rbx
    mov     rdi, r12                                    // try to restore context
    call    aio_context_switch

    pop     rbp                                         // unreachable
    ret

    .text
    .global aio_context_create
aio_context_create:
    lea     rsi, [rsi + rdx]                            // calculate stack top
    and     rsi, -0x10                                  // align stack by 16 bytes
    sub     rsi, 8
    mov     QWORD PTR [rsi], 0                          // store NULL return address

    lea     r10, [rip+aio_context_trampoline@plt]
    mov     [rdi], r10                                  // store rip
    mov     [rdi + 8], rsi                              // store rsp
    mov     [rdi + 16], rsi                             // store rbp
    mov     [rdi + 24], rcx                             // store rbx (entrypoint)
    mov     [rdi + 32], rdi                             // store r12 (context address)

    ret

#else

#error Unsupported platform

#endif
