cmake_minimum_required(VERSION 3.13)
project(opencl_headers)

# Khronos OpenCL 1.2 C headers; the runtime is loaded dynamically, nothing is linked.
add_library(opencl_headers INTERFACE)

target_include_directories(opencl_headers SYSTEM
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
