add_executable(kv_bench main.cpp)
target_link_libraries(kv_bench PRIVATE Threads::Threads)
