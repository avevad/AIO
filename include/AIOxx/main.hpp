#pragma once

#include <type_traits>

#include "event_loop.hpp"

namespace _AIO {
void main(AIO::BasicEventLoop &loop, int argc, const char *const *argv);
}

#define AIO_MAIN                                                                                                       \
  /*... return-type */ _aio_ignored();                                                                                 \
  int main(int argc, const char *const *argv) {                                                                        \
    AIO::run([=](AIO::BasicEventLoop &loop) { _AIO::main(loop, argc, argv); });                                        \
  }                                                                                                                    \
  std::invoke_result_t<decltype(&_aio_ignored)> _AIO::main /* args&body ...*/
