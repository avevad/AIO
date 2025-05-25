#include "context.hpp"
#include "coroutine.hpp"
#include "event_loop.hpp"

#include <iostream>
#include <memory>
#include <variant>
#include <utility>

void sample_contexts() {
    std::cout << "-----------Contexts-----------" << std::endl;

    static AIO::context_t context{};
    constexpr static std::size_t STACK_SIZE_BYTES = 16 * 1024; // 16 KiB

    const auto stack = std::make_unique<char[]>(STACK_SIZE_BYTES);

    auto subcontext_entrypoint = []() -> AIO::context_t & {
        std::cout << "Hello from subcontext" << std::endl;
        switch_to_context(context);
        std::cout << "Subcontext will exit now" << std::endl;
        return context;
    };

    context = AIO::make_context(subcontext_entrypoint, stack.get(), STACK_SIZE_BYTES);

    std::cout << "Hello from main" << std::endl;
    switch_to_context(context);
    std::cout << "Finishing the subcontext" << std::endl;
    switch_to_context(context);
    std::cout << "Done" << std::endl;
}

void sample_coroutines() {
    std::cout << "----------Coroutines----------" << std::endl;

    constexpr int MAX = 100000;
    AIO::Coroutine<int()> fib = [&fib] [[noreturn]] () -> int {
        int prev = 0, cur = 1;
        while (true) {
            if (cur > MAX)
                throw AIO::EndGeneration();

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

    constexpr size_t M = 5;
    std::vector<int> more_fibs;
    more_fibs.reserve(M);
    std::copy_n(AIO::CoroutineIterator(fib), M, std::back_inserter(more_fibs));
    std::cout << "More fibs: ";
    for (const int e : more_fibs) {
        std::cout << e << ' ';
    }
    std::cout << "..." << std::endl;


    std::cout << "Until MAX=" << MAX << ": ";
    for (const int e : AIO::CoroutineGenerator(fib)) {
        std::cout << e << ' ';
    }
    std::cout << std::endl;
}

void sample_event_loop() {
    std::cout << "----------Event loop----------" << std::endl;

    AIO::run([](auto &loop) -> void {
        auto calculate = loop.async([] -> int {
            std::cout << "Calculating the number..." << std::endl;
            return 42;
        });

        auto print_hello = loop.async([] -> std::monostate {
            std::cout << "Hello from asynchronous task!" << std::endl;
            return {};
        });

        std::cout << "Beginning of main" << std::endl;
        print_hello().drop();

        std::cout << "Starting calculation..." << std::endl;
        auto future = calculate();
        std::cout << "Started calculate() function" << std::endl;

        auto result = loop.await(std::move(future));
        std::cout << "Result: " << result << std::endl;
    });
}

int main() {
    sample_contexts();
    sample_coroutines();
    sample_event_loop();
}
