cmake_minimum_required(VERSION 3.21)
project(kv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

option(KV_BUILD_TESTS "Build the test suite" ON)
option(KV_ENABLE_TSAN "Build with ThreadSanitizer to catch data races" OFF)

if(KV_ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -g)
  add_link_options(-fsanitize=thread)
endif()

find_package(Threads REQUIRED)

add_subdirectory(src)
add_subdirectory(server)

if(KV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
