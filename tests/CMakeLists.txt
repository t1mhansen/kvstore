include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(kv_tests
  crc32_test.cpp
  log_record_test.cpp
  storage_engine_test.cpp
)
target_include_directories(kv_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kv_tests PRIVATE kv_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(kv_tests)
