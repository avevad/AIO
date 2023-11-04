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

/*
 * Output:
 *
 * 0: 1
 * 1: 1
 * 2: 2
 * 3: 3
 * 4: 5
 * 5: 8
 * 6: 13
 * 7: 21
 * 8: 34
 * 9: 55
 * 10: 89
 * 11: 144
 * 12: 233
 * 13: 377
 * 14: 610
 * 15: 987
 * 16: 1597
 * 17: 2584
 * 18: 4181
 * 19: 6765
 * 20: 10946
 * 21: 17711
 * 22: 28657
 * 23: 46368
 * 24: 75025
 * 25: 121393
 * 26: 196418
 * 27: 317811
 * 28: 514229
 * 29: 832040
 *
 * Process finished with exit code 0
 */