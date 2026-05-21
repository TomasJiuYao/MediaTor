cmake_minimum_required(VERSION 3.16)
project(MediaTer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(BUILDROOT_DIR /home/yj/rk3588_sdk/rk3588-vision/OK3588-linux-source/buildroot/output/rockchip_rk3588_recovery)

set(CMAKE_C_COMPILER ${BUILDROOT_DIR}/host/bin/aarch64-linux-gcc)
set(CMAKE_CXX_COMPILER ${BUILDROOT_DIR}/host/bin/aarch64-linux-g++)

set(TOOLCHAIN_SYSROOT ${BUILDROOT_DIR}/host/aarch64-buildroot-linux-gnu/sysroot)

set(CMAKE_SYSROOT ${TOOLCHAIN_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# FFmpeg headers
set(FFMPEG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/include/ffmpeg)

# FFmpeg static libraries
set(FFMPEG_LIB_DIR ${CMAKE_SOURCE_DIR}/lib/ffmpeg)

# Output executables to release/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/release)

add_subdirectory(src/common)
add_subdirectory(src/v4l2)
add_subdirectory(src/encoder)
add_subdirectory(src/rtsp)
add_subdirectory(src/app)
