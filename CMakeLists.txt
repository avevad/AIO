cmake_minimum_required(VERSION 3.5)
project(aio LANGUAGES CXX ASM)

option(AIO_BUILD_TESTS "Build unittests for AIO" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")
set(CMAKE_ASM_CREATE_SHARED_LIBRARY ${CMAKE_CXX_CREATE_SHARED_LIBRARY})

include(FetchContent)

if (AIO_BUILD_TESTS)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif ()

# Shared library
add_library(aio-static STATIC
        src/util.cpp
        src/context.S
        src/context.cpp
)
target_include_directories(aio-static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(aio-static PROPERTIES OUTPUT_NAME aio)

# Static library
add_library(aio-shared SHARED
        src/util.cpp
        src/context.S
        src/context.cpp
)
target_include_directories(aio-shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(aio-shared PROPERTIES OUTPUT_NAME aio)

# Sample executable
add_executable(sample src/sample.cpp)
target_include_directories(sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sample PRIVATE aio-static)

enable_testing()
if (AIO_BUILD_TESTS)
    # Unit tests executable
    add_executable(ut src/ut.cpp)
    target_include_directories(ut PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(ut PRIVATE aio-static GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(ut)
endif ()

include(GNUInstallDirs)
