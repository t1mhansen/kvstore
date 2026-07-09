add_library(kv_server_lib STATIC
  protocol.cpp
  tcp_server.cpp
)
target_link_libraries(kv_server_lib PUBLIC kv_core Threads::Threads)

add_executable(kv_server main.cpp)
target_link_libraries(kv_server PRIVATE kv_server_lib)
