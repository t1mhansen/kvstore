add_library(kv_core STATIC
  crc32.cpp
  file_handle.cpp
  log_record.cpp
  storage_engine.cpp
)
target_include_directories(kv_core
  PUBLIC ${CMAKE_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
