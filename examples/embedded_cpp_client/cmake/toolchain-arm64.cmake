# CMake toolchain file for cross-compiling the Flower C++ client to 64-bit ARM
# Linux (e.g. NanoPi boards with Cortex-A53+ SoCs). Use with:
#   cmake --preset linux-arm64-cross
#
# Requires the aarch64 GNU cross compiler on the host. Override the compilers
# with -DCMAKE_C_COMPILER=... -DCMAKE_CXX_COMPILER=... if needed.
# Set FLWR_ARM64_SYSROOT to the target root filesystem to link against
# target libraries.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
endif()
if(NOT CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

# Host tools (protoc, grpc_cpp_plugin) are found through PATH, not the sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

if(FLWR_ARM64_SYSROOT)
  set(CMAKE_SYSROOT "${FLWR_ARM64_SYSROOT}")
  set(CMAKE_FIND_ROOT_PATH "${FLWR_ARM64_SYSROOT}")
endif()
