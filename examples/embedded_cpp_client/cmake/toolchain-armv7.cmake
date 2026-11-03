# CMake toolchain file for cross-compiling the Flower C++ client to 32-bit
# ARM Linux (Cortex-A7, e.g. NanoPi). Use with:
#   cmake --preset linux-armv7-cross -DFLWR_ARMV7_SYSROOT=<target-rootfs>
# FLWR_ARMV7_SYSROOT may also be set in the environment instead of via -D.
#
# Cortex-A7 is ARMv7-A hard-float (gnueabihf); an aarch64 cross compiler
# cannot target it. The compiler is resolved in this order (first hit wins),
# and CMAKE_C_COMPILER / CMAKE_CXX_COMPILER override it when you set them:
#   1. <FLWR_ARMV7_SYSROOT>/usr/bin/<name>
#   2. <name> on PATH
# with <name> tried as:
#   arm-linux-gnueabihf-gcc/g++   (Debian gcc-arm-linux-gnueabihf; also the
#                                  layout documented in README_nanoPi.md,
#                                  where the toolchain lives inside the sysroot)
#   arm-none-linux-gnueabihf-gcc/g++ (Arm GNU Toolchain, self-contained)
#   arm-linux-gnueabi-gcc/g++     (Fedora gcc-arm-linux-gnu)
#   arm-linux-gnu-gcc/g++         (Fedora binary name for the above)
#
# A sysroot (target rootfs) is required to link: it must carry glibc dev
# files (crt1.o, libc.so, headers) and OpenSSL 3 dev files. For the Debian
# cross toolchain the sysroot's multiarch layout (/usr/include/c++/<ver>,
# /usr/lib/gcc/<triple>/<ver>) is wired up automatically below; a bare Arm
# GNU toolchain brings its own sysroot and needs none of that.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---------------------------------------------------------------------------
# Sysroot
# ---------------------------------------------------------------------------
if(NOT FLWR_ARMV7_SYSROOT AND DEFINED ENV{FLWR_ARMV7_SYSROOT})
  set(FLWR_ARMV7_SYSROOT "$ENV{FLWR_ARMV7_SYSROOT}")
endif()

# ---------------------------------------------------------------------------
# Compilers
# ---------------------------------------------------------------------------
set(_FLWR_C_NAMES
  arm-linux-gnueabihf-gcc
  arm-none-linux-gnueabihf-gcc
  arm-linux-gnueabi-gcc
  arm-linux-gnu-gcc)
set(_FLWR_CXX_NAMES
  arm-linux-gnueabihf-g++
  arm-none-linux-gnueabihf-g++
  arm-linux-gnueabi-g++
  arm-linux-gnu-g++)
set(_FLWR_HINTS "")
if(FLWR_ARMV7_SYSROOT)
  list(APPEND _FLWR_HINTS "${FLWR_ARMV7_SYSROOT}/usr/bin")
endif()

if(NOT CMAKE_C_COMPILER)
  find_program(FLWR_C_COMPILER NAMES ${_FLWR_C_NAMES} HINTS ${_FLWR_HINTS})
  if(NOT FLWR_C_COMPILER)
    message(FATAL_ERROR
      "No ARM C cross compiler found. Searched for ${_FLWR_C_NAMES} in "
      "<FLWR_ARMV7_SYSROOT>/usr/bin and PATH.\n"
      "Install one, e.g. on Debian/Ubuntu: apt install gcc-arm-linux-gnueabihf\n"
      "(see README_nanoPi.md for the sysroot layout this example expects), or "
      "pass -DCMAKE_C_COMPILER=<path>.")
  endif()
  set(CMAKE_C_COMPILER "${FLWR_C_COMPILER}")
endif()
if(NOT CMAKE_CXX_COMPILER)
  find_program(FLWR_CXX_COMPILER NAMES ${_FLWR_CXX_NAMES} HINTS ${_FLWR_HINTS})
  if(NOT FLWR_CXX_COMPILER)
    message(FATAL_ERROR
      "Found the C compiler (${CMAKE_C_COMPILER}) but no ARM C++ compiler. "
      "Searched for ${_FLWR_CXX_NAMES} in <FLWR_ARMV7_SYSROOT>/usr/bin and "
      "PATH.\n"
      "Install the g++ counterpart (e.g. g++-arm-linux-gnueabihf on "
      "Debian/Ubuntu, gcc-c++-arm-linux-gnu on Fedora) or pass "
      "-DCMAKE_CXX_COMPILER=<path>.")
  endif()
  set(CMAKE_CXX_COMPILER "${FLWR_CXX_COMPILER}")
endif()

# Cortex-A7 tuning: ARMv7-A with NEON + VFPv4, hard-float ABI. The *_INIT
# variables keep these flags overridable from the command line.
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")

# Host tools (protoc, grpc_cpp_plugin) are found through PATH, not the sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---------------------------------------------------------------------------
# Sysroot wiring
# ---------------------------------------------------------------------------
if(FLWR_ARMV7_SYSROOT)
  set(CMAKE_SYSROOT "${FLWR_ARMV7_SYSROOT}")
  set(CMAKE_FIND_ROOT_PATH "${FLWR_ARMV7_SYSROOT}")

  # Debian multiarch layout. When the cross compiler itself lives inside the
  # sysroot, it relocates its program/library search paths but drops its
  # configured /usr/include/c++ and /usr/lib/gcc/<triple>/<ver> include
  # paths (they do not exist on the host). Re-add the ones the sysroot
  # actually has, as absolute paths (gcc does not prefix -isystem/-B/-L with
  # the sysroot, so these are taken verbatim).
  set(_FLWR_EXTRA_INC "")
  set(_FLWR_EXTRA_LINK "")
  file(GLOB _FLWR_CXX_INC "${FLWR_ARMV7_SYSROOT}/usr/include/c++/*")
  file(GLOB _FLWR_MA_CXX_INC "${FLWR_ARMV7_SYSROOT}/usr/include/arm-linux-*/c++/*")
  file(GLOB _FLWR_GCC_DIRS "${FLWR_ARMV7_SYSROOT}/usr/lib/gcc/arm-linux-*/*")
  foreach(d IN LISTS _FLWR_GCC_DIRS)
    if(EXISTS "${d}/include")            # gcc's fixed headers (stddef.h, ...)
      string(APPEND _FLWR_EXTRA_INC " -isystem \"${d}/include\"")
    endif()
    string(APPEND _FLWR_EXTRA_LINK " -B\"${d}/\" -L\"${d}\"")  # crt*.o, libgcc, libstdc++
  endforeach()
  foreach(d IN LISTS _FLWR_CXX_INC _FLWR_MA_CXX_INC)
    string(APPEND _FLWR_EXTRA_INC " -isystem \"${d}\"")
  endforeach()
  string(APPEND CMAKE_C_FLAGS_INIT "${_FLWR_EXTRA_INC}")
  string(APPEND CMAKE_CXX_FLAGS_INIT "${_FLWR_EXTRA_INC}")
  string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT "${_FLWR_EXTRA_LINK}")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT "${_FLWR_EXTRA_LINK}")
  string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT "${_FLWR_EXTRA_LINK}")

  # Lets find_library() try <root>/usr/lib/<arch> (Debian multiarch), where
  # the libssl.so/libcrypto.so dev symlinks live.
  if(EXISTS "${FLWR_ARMV7_SYSROOT}/usr/lib/arm-linux-gnueabihf")
    set(CMAKE_LIBRARY_ARCHITECTURE "arm-linux-gnueabihf")
  endif()

  # Point pkg-config at the sysroot so configure checks (c-ares/systemd, ...)
  # resolve target libraries instead of the host's x86 ones.
  if(IS_DIRECTORY "${FLWR_ARMV7_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig")
    set(ENV{PKG_CONFIG_LIBDIR}
        "${FLWR_ARMV7_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig:${FLWR_ARMV7_SYSROOT}/usr/share/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "")
  endif()
endif()