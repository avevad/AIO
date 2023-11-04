#include "aio.hpp"

static constexpr size_t N = 30;

int main() {
    AIO::Coroutine<size_t()> fib = [&fib] [[noreturn]]() -> size_t {
        size_t prev_num = 0, cur_num = 1;
        while (true) {
            fib.yield(cur_num);
            size_t next_num = prev_num + cur_num;
            prev_num = cur_num;
            cur_num = next_num;
        }
    };
    for (size_t pos = 0; pos < N; pos++) {
        std::cout << pos << ": " << fib.resume() << std::endl;
    }
}