#include "context.hpp"

#include <memory>
#include <iostream>

AIO::aio_context context{};

void subcontext_entrypoint() {
    std::cout << "Hello from subcontext" << std::endl;
    aio_context_switch(&context);
    std::cout << "Subcontext will exit now" << std::endl;
}

int main() {
    constexpr static std::size_t STACK_SIZE_BYTES = 16 * 1024; // 16 KiB

    auto stack = std::make_unique<char[]>(STACK_SIZE_BYTES);
    char *stack_beg = stack.get();
    aio_context_create(&context, stack_beg + STACK_SIZE_BYTES - 8, subcontext_entrypoint);

    std::cout << "Hello from main" << std::endl;
    aio_context_switch(&context);
    std::cout << "Finishing the subcontext" << std::endl;
    aio_context_switch(&context);
    std::cout << "Done" << std::endl;
}
