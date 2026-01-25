#include "AIOxx/coroutine.hpp"

namespace AIO::_impl {
thread_local void *volatile current_coroutine = nullptr;
}
