# INTERFACE for now: no .cpp files exist yet. Milestone 2 (storage engine)
# turns this into a real STATIC library once there's something to compile.
add_library(kv_core INTERFACE)
target_include_directories(kv_core INTERFACE ${CMAKE_SOURCE_DIR}/include)
