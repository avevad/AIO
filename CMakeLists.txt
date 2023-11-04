cmake_minimum_required(VERSION 3.5)
project(aio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

IF (CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,leak,undefined -fno-sanitize-recover")
    add_definitions(-D_GLIBCXX_DEBUG)
ENDIF ()

add_executable(sample src/sample.cpp)
target_include_directories(sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

include(GNUInstallDirs)
