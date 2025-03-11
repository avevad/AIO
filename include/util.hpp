#pragma once

#include <iostream>
#include <source_location>
#include <string>

namespace AIO {

    [[noreturn]] void
    assertion_failed(const std::string &what, std::source_location where = std::source_location::current());

}
