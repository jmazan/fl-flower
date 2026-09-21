cmake_minimum_required(VERSION 3.16)
project(EmbeddedCppClient VERSION 0.1
  DESCRIPTION "Native C++ Flower Fleet client for embedded devices (NanoPI)"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Path to the Flower repository root. Defaults to the parent of this example
# (i.e. the checkout this example lives in). Override with
#   -DFLWR_SOURCE_ROOT=/path/to/flower
set(FLWR_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH
    "Path to the Flower repository root (contains framework/proto)")
# Normalize the path (protoc does not resolve ".." in --proto_path).
get_filename_component(FLWR_SOURCE_ROOT "${FLWR_SOURCE_ROOT}" ABSOLUTE)

# gRPC version pinned to v1.78.1 (latest stable). The framework's Python
# dependency is `grpcio>=1.70.0` (framework/pyproject.toml); the C++ gRPC wire
# protocol is stable across versions, so a newer C++ gRPC is compatible.
# Older gRPC versions do not build with modern toolchains:
#   - v1.43.2 (legacy SDK): bundled abseil fails with GCC 15
#   - v1.70.1: bundled protobuf utf8_range install(EXPORT) fails with CMake 4.x
# Upgrade deliberately, not implicitly.
set(GRPC_VERSION "v1.78.1" CACHE STRING "gRPC version to fetch")

# ---------------------------------------------------------------------------
# Download gRPC
# ---------------------------------------------------------------------------
include(FetchContent)

# protobuf's install(EXPORT) rules are incompatible with CMake 4.x
# (targets referencing abseil targets not in the export set). We only need the
# libraries, not installation, so disable it. These must be set BEFORE
# FetchContent_MakeAvailable(gRPC) because gRPC's CMakeLists reads them when
# it adds the protobuf subdirectory.
set(protobuf_INSTALL OFF CACHE BOOL "Install protobuf binaries and files" FORCE)
set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "Configure installation" FORCE)

FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG        ${GRPC_VERSION}
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(gRPC)

set(_PROTOBUF_LIBPROTOBUF libprotobuf)
set(_REFLECTION grpc++_reflection)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc>)
set(_GRPC_GRPCPP grpc++)
if(CMAKE_CROSSCOMPILING)
    # When cross-compiling for ARM, protoc and grpc_cpp_plugin must be built
    # for the host and provided via CMAKE_PROGRAM_PATH / PATH.
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)
else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)
endif()

# ---------------------------------------------------------------------------
# Generate current Flower protobufs into the build tree
# ---------------------------------------------------------------------------
# The proto files live in the Flower repository under framework/proto. They are
# generated at build time into ${CMAKE_CURRENT_BINARY_DIR}/gen and are never
# copied into the source tree.
set(FLWR_PROTO_BASE_PATH "${FLWR_SOURCE_ROOT}/framework/proto")
# protoc preserves the package directory structure (flwr/proto/...) relative to
# the proto_path, so the output root is ${CMAKE_CURRENT_BINARY_DIR}/gen.
set(GEN_PROTO_ROOT "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(GEN_PROTO_DIR "${GEN_PROTO_ROOT}/flwr/proto")
file(MAKE_DIRECTORY "${GEN_PROTO_DIR}")

# Protos needed by the Fleet client. Order matters: dependencies first.
set(FLWR_PROTOS
  transport
  error
  node
  recorddict
  message
  heartbeat
  run
  fab
  fleet
)

set(ALL_GEN_SRCS "")

foreach(PROTO_NAME ${FLWR_PROTOS})
  set(OUT_PROTO_SRCS "${GEN_PROTO_DIR}/${PROTO_NAME}.pb.cc")
  set(OUT_PROTO_HDRS "${GEN_PROTO_DIR}/${PROTO_NAME}.pb.h")
  set(OUT_GRPC_SRCS "${GEN_PROTO_DIR}/${PROTO_NAME}.grpc.pb.cc")
  set(OUT_GRPC_HDRS "${GEN_PROTO_DIR}/${PROTO_NAME}.grpc.pb.h")
  set(SOURCE_PROTO "${FLWR_PROTO_BASE_PATH}/flwr/proto/${PROTO_NAME}.proto")

  add_custom_command(
    OUTPUT "${OUT_PROTO_SRCS}" "${OUT_PROTO_HDRS}"
           "${OUT_GRPC_SRCS}" "${OUT_GRPC_HDRS}"
    COMMAND ${_PROTOBUF_PROTOC}
            --proto_path=${FLWR_PROTO_BASE_PATH}
            --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
            --grpc_out=${GEN_PROTO_ROOT}
            --cpp_out=${GEN_PROTO_ROOT}
            ${SOURCE_PROTO}
    DEPENDS "${SOURCE_PROTO}"
    COMMENT "Generating protobuf/gRPC sources for ${PROTO_NAME}.proto"
    VERBATIM
  )

  list(APPEND ALL_GEN_SRCS
    "${OUT_PROTO_SRCS}" "${OUT_GRPC_SRCS}")
endforeach()

# ---------------------------------------------------------------------------
# FLWR_PROTO library: generated protocol code only
# ---------------------------------------------------------------------------
add_library(flwr_proto STATIC ${ALL_GEN_SRCS})
target_include_directories(flwr_proto PUBLIC
  "${GEN_PROTO_ROOT}"
)
target_link_libraries(flwr_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF}
)

# ---------------------------------------------------------------------------
# FLWR_CLIENT: the native Fleet client executable
# ---------------------------------------------------------------------------
set(FLWR_CLIENT_SRCS
  src/main.cc
  src/flower_client.cc
  src/fleet_transport.cc
  src/heartbeat.cc
  src/message_codec.cc
  src/parameter_codec.cc
  src/trainer.cc
)

set(EXECUTABLE_NAME flwr_client)
add_executable(${EXECUTABLE_NAME} ${FLWR_CLIENT_SRCS})

target_include_directories(${EXECUTABLE_NAME} PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(${EXECUTABLE_NAME}
  flwr_proto
)