#include "AIOxx/util.hpp"

#include <cxxabi.h>

namespace {

std::string make_exception_message(const std::string &what, std::exception_ptr err) {
  try {
    std::rethrow_exception(std::move(err));
  } catch (std::exception &e) {
    int flag = 0;
    char *e_name_c = abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &flag);
    const std::string e_name = e_name_c;
    std::free(e_name_c);
    return what + ": " + e_name + ": " + e.what();
  } catch (...) {
    return what + ": <unknown exception>";
  }
}

} // namespace

void AIO::panic(const std::string &what, const std::source_location where) {
  std::cerr << std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                 std::to_string(where.column()) +
                 ": "
                 "function ‘" +
                 where.function_name() + "’: "
            << what << std::endl;
  std::abort();
}

void AIO::panic(const std::string &what, std::exception_ptr err, const std::source_location where) {
  panic(make_exception_message(what, std::move(err)), where);
}

void AIO::warning(const std::string &what, const std::source_location where) {
#ifdef AIOXX_DEBUG
  std::cerr << std::string(where.file_name()) + ":" + std::to_string(where.line()) + ":" +
                 std::to_string(where.column()) +
                 ": "
                 "function ‘" +
                 where.function_name() +
                 "’: "
                 "runtime warning: " +
                 what
            << std::endl;
#else
  (void) what;
  (void) where;
#endif
}

void AIO::warning(const std::string &what, std::exception_ptr err, const std::source_location where) {
  warning(make_exception_message(what, std::move(err)), where);
}
