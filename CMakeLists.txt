cmake_minimum_required(VERSION 3.5)
project(aio LANGUAGES CXX ASM)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_ASM_CREATE_SHARED_LIBRARY ${CMAKE_CXX_CREATE_SHARED_LIBRARY})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")

# Shared library
add_library(aio-static STATIC
        src/context.S
        src/util.cpp
)
target_include_directories(aio-static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(aio-static PROPERTIES OUTPUT_NAME aio)

# Static library
add_library(aio-shared SHARED
        src/context.S
        src/util.cpp
)
target_include_directories(aio-shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(aio-shared PROPERTIES OUTPUT_NAME aio)

# Sample executable
add_executable(sample src/sample.cpp)
target_include_directories(sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sample PRIVATE aio-static)

include(GNUInstallDirs)
