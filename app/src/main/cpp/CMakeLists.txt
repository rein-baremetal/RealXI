cmake_minimum_required(VERSION 3.22.1)
project(realxi_native LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The engine is plain C++ and also builds on desktop (see engine/CMakeLists.txt and `make engine-test`).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine ${CMAKE_CURRENT_BINARY_DIR}/engine)

add_library(realxi_native SHARED jni_bridge.cpp)
target_link_libraries(realxi_native realxi_engine)

# Android 15+ devices with 16 KB pages require 16 KB aligned native libraries.
target_link_options(realxi_native PRIVATE -Wl,-z,max-page-size=16384)
