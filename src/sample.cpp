#include "context.hpp"
#include "coroutine.h"

#include <memory>
#include <iostream>

void sample_contexts() {
    std::cout << "-----------Contexts-----------" << std::endl;

    static AIO::aio_context context { };
    constexpr static std::size_t STACK_SIZE_BYTES = 16 * 1024; // 16 KiB

    const auto stack = std::make_unique<char[]>(STACK_SIZE_BYTES);
    char *stack_beg = stack.get();

    auto subcontext_entrypoint = []() -> void {
        std::cout << "Hello from subcontext" << std::endl;
        aio_context_switch(&context);
        std::cout << "Subcontext will exit now" << std::endl;
    };

    aio_context_create(&context, stack_beg + STACK_SIZE_BYTES - 8, subcontext_entrypoint);

    std::cout << "Hello from main" << std::endl;
    aio_context_switch(&context);
    std::cout << "Finishing the subcontext" << std::endl;
    aio_context_switch(&context);
    std::cout << "Done" << std::endl;
}

void sample_coroutines() {
    std::cout << "----------Coroutines----------" << std::endl;

    AIO::Coroutine<int()> fib = [&fib] [[noreturn]] () -> int {
        int prev = 0, cur = 1;
        while (true) {
            fib.yield(cur);
            const int next = prev + cur;
            prev = cur;
            cur = next;
        }
    };

    constexpr size_t N = 10;
    for (size_t i = 1; i <= N; i++) {
        std::cout << "fib[" << i << "] = " << fib.resume() << std::endl;
    }
}

int main() {
    sample_contexts();
    sample_coroutines();
}
