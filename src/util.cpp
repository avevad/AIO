#include "AIOxx/util.hpp"

#include <cxxabi.h>

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

std::string make_exception_message(const std::string &what, const std::exception &e) {
  int flag = 0;
  char *e_name_c = abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &flag);
  std::string e_name = e_name_c;
  std::free(e_name_c);
  return what + ": " + e_name + ": " + e.what();
}

void AIO::assertion_failed(const std::string &what, const std::exception &e, std::source_location where) {
  assertion_failed(make_exception_message(what, e), where);
}

void AIO::issue_warning(
  const std::string __attribute_maybe_unused__ &what, std::source_location __attribute_maybe_unused__ where) {
#ifdef AIOXX_OPT_RUNTIME_WARNINGS
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

void AIO::issue_warning(const std::string &what, const std::exception &e, std::source_location where) {
  issue_warning(make_exception_message(what, e), where);
}
