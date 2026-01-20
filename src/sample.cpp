#include "AIOxx/context.hpp"
#include "AIOxx/coroutine.hpp"
#include "AIOxx/event_loop.hpp"
#include "AIOxx/fd.hpp"

#include <iostream>
#include <memory>
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

  /* -----------Contexts-----------
   * Hello from main
   * Hello from subcontext
   * Finishing the subcontext
   * Subcontext will exit now
   * Done
   */
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

  /* ----------Coroutines----------
   * fib[1] = 1
   * fib[2] = 1
   * fib[3] = 2
   * fib[4] = 3
   * fib[5] = 5
   * fib[6] = 8
   * fib[7] = 13
   * fib[8] = 21
   * fib[9] = 34
   * fib[10] = 55
   * More fibs: 89 144 233 377 610 ...
   * Until MAX=100000: 987 1597 2584 4181 6765 10946 17711 28657 46368 75025
   */
}

void sample_event_loop() {
  using namespace std::chrono_literals;
  std::cout << "----------Event loop----------" << std::endl;

  AIO::run_in_new([](AIO::BasicEventLoop *loop) -> void {
    auto calculated = loop->async([&loop] -> int {
      std::cout << "Calculating the number..." << std::endl;
      loop->await(loop->timeout(1s));
      return 21;
    });

    auto multiplied = loop->async([&loop](int x) -> int {
      std::cout << "Multiplying " << x << " by 2..." << std::endl;
      loop->await(loop->timeout(1s));
      return x * 2;
    });

    auto hello_printed =
      loop->async([] -> void { std::cout << "  Hello from some noisy background task!" << std::endl; });

    auto small_delay = [&loop]() { return loop->timeout(500ms); };

    auto user_secret_obtained = loop->async([&loop](const std::optional<std::string> &user_name) -> std::string {
      std::cout << "Checking username..." << std::endl;
      loop->await(loop->timeout(1s));
      if (user_name.value().length() > 10) {
        throw std::length_error("username is too long");
      }
      std::cout << "Obtaining user data..." << std::endl;
      loop->await(loop->timeout(2s));
      if (user_name.value() != "avevad") {
        throw std::invalid_argument("user not found");
      }
      return "lorem ipsum";
    });

    std::cout << "Beginning of main" << std::endl;

    // Add some noise
    small_delay()
      .then(hello_printed)
      .then(small_delay)
      .then(hello_printed)
      .then(small_delay)
      .then(hello_printed)
      .detach();

    std::cout << "Starting calculation..." << std::endl;
    auto ready = calculated().then(multiplied);
    std::cout << "Started calculate() function" << std::endl;

    auto result = loop->await(std::move(ready));
    std::cout << "Result: " << result << std::endl;

    std::optional<std::string> user_name;
    std::cout << "Enter your name: ";
    std::cout.flush();
    // Don't do actual reading - just wait for *some* data -- if STDIN is a terminal,
    // then a whole line would be ready for consequent std::istream read
    if (loop->await(loop->std_in().ready(AIO::FD::IN) | loop->timeout(5s))) {
      std::cout << "Got it!" << std::endl;

      std::string name;
      std::getline(std::cin, name);

      std::cout << "Hello, " << name << "" << std::endl;
      user_name = name;
    } else {
      std::cout << "(timeout)" << std::endl;
    }

    // Add more noise
    small_delay()
      .then(hello_printed)
      .then(small_delay)
      .then(hello_printed)
      .then(small_delay)
      .then(hello_printed)
      .detach();

    try {
      loop->await(user_secret_obtained(user_name)
                    .then(loop->async([](const std::string &secret) {
                      std::cout << "Here is your secret: '" << secret << "'" << std::endl;
                    }))
                    .except<std::invalid_argument>(loop->async([](const std::invalid_argument &e) {
                      std::cout << "! Invalid input: " << e.what() << std::endl;
                    }))
                    .except<std::length_error>(loop->async([](std::length_error e) {
                      std::cout << "! Overflow: " << e.what() << std::endl;
                    })));
    } catch (std::bad_optional_access &) {
      // unhandled by .except()-clauses exceptions will be thrown out of await()
      std::cout << "No input detected in 5 seconds" << std::endl;
    }

    std::cout << "Done" << std::endl;
  });

  /* ----------Event loop----------
   * Beginning of main
   * Starting calculation...
   * Started calculate() function
   * Hello from some asynchronous task!
   * Calculating the number...
   * Hello from some asynchronous task!
   * Hello from some asynchronous task!
   * Multiplying 21 by 2...
   * Result: 42
   * Enter your name: (timeout)
   */
}

int main() {
  sample_contexts();
  sample_coroutines();
  sample_event_loop();
}
