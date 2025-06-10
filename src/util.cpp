#include "util.hpp"

void AIO::assertion_failed(const std::string &what, const std::source_location where) {
    std::cerr << std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                     std::to_string(where.column()) +
                     ": "
                     "function ‘" +
                     where.function_name() +
                     "’: "
                     "assertion failed: " +
                     what
              << std::endl;
    std::abort();
}

void AIO::issue_warning(
    const std::string __attribute_maybe_unused__ &what, std::source_location __attribute_maybe_unused__ where
) {
#ifdef AIO_OPT_RUNTIME_WARNINGS
    if (!std::getenv("AIO_NO_RUNTIME_WARNINGS")) {
        std::cerr << std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                         std::to_string(where.column()) +
                         ": "
                         "function ‘" +
                         where.function_name() +
                         "’: "
                         "runtime warning: " +
                         what
                  << std::endl;
    }
#else
#endif
}
