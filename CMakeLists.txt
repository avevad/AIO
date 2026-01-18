cmake_minimum_required(VERSION 3.5)
project(AIOxx LANGUAGES CXX ASM)

cmake_policy(SET CMP0077 NEW)

option(AIOXX_BUILD_TESTS            "Build AIO++ unittests" ON)
option(AIOXX_INSTALL                "Enable AIO++ installation" ON)

set(CMAKE_CXX_STANDARD              23)
set(CMAKE_CXX_STANDARD_REQUIRED     ON)
set(CMAKE_CXX_FLAGS                 "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")
set(CMAKE_ASM_CREATE_SHARED_LIBRARY ${CMAKE_CXX_CREATE_SHARED_LIBRARY})

# https://github.com/google/googletest/issues/4762
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "21.0.0")
        set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} -Wno-error=assume -Wno-error=character-conversion")
    endif()
endif ()

include(FetchContent)

if (AIOXX_BUILD_TESTS)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )

    set(gtest_force_shared_crt      ON  CACHE BOOL "" FORCE)
    set(INSTALL_GTEST               OFF)
    SET(INSTALL_GMOCK               OFF)
    FetchContent_MakeAvailable(googletest)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(AIOXX_DEBUG=1)
endif ()

# Shared library
add_library(aio-static STATIC
        src/util.cpp
        src/context.S
        src/context.cpp
        src/io.cpp
        src/fd.cpp
        src/net_fd.cpp
        src/main.cpp
)
target_include_directories(aio-static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(aio-static PROPERTIES OUTPUT_NAME aioxx)

# Static library
add_library(aio-shared SHARED
        src/util.cpp
        src/context.S
        src/context.cpp
        src/io.cpp
        src/fd.cpp
        src/net_fd.cpp
        src/main.cpp
)
target_include_directories(aio-shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(aio-shared PROPERTIES OUTPUT_NAME aioxx)

# Sample executable
add_executable(sample src/sample.cpp)
target_link_libraries(sample PRIVATE aio-static)

# Sample chat server
add_executable(sample_server src/sample_server.cpp)
target_link_libraries(sample_server PRIVATE aio-static)

if (AIOXX_BUILD_TESTS)
    enable_testing()
    include(GoogleTest)

    # Unit tests executable
    add_executable(ut src/ut.cpp)
    target_include_directories(ut PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(ut PRIVATE aio-static GTest::gtest_main)

    gtest_discover_tests(ut)
endif ()

if (AIOXX_INSTALL)
    install(TARGETS aio-static aio-shared)
    install(DIRECTORY include/AIOxx DESTINATION include)
endif ()
