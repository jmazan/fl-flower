---
tags: [quickstart, linear regression, tabular, embedded]
dataset: [Synthetic]
framework: [C++]
---

# Flower Clients in C++ (embedded, NanoPI)

> [!WARNING]\
> This example is under development. It implements Phases 1 and 2 of the
> design in [`HLD_embedded_client.md`](./HLD_embedded_client.md): a native C++
> Fleet client that connects to a Flower SuperLink and exchanges inline
> `Message`/`RecordDict` payloads with a legacy Python strategy.

In this example you will train a linear model on synthetic data using a native
C++ client that runs on an embedded device (e.g. NanoPI). The client speaks the
current Flower Fleet gRPC API directly — no Python, `ClientApp`, or `flwr run`
is needed on the device.

```text
NanoPI                                      Remote machine
                                               |
Native C++ Fleet client ------------ gRPC ---- Flower SuperLink
                                               |
                                      Python ServerApp / Strategy
```

## Acknowledgements

Many thanks to the original contributors to the `quickstart-cpp` example this
is based on:

- Lekang Jiang (original author and main contributor)
- Francisco José Solís (code re-organization)
- Andreea Zaharia (training algorithm and data generation)

## Requirements

- CMake >= 3.16
- A C++17 compiler
- Python with `flwr` installed (for the server side only)
- The Flower repository checkout (this example generates the current protobuf
  sources from `framework/proto` at build time)

## Building the client

Build on Linux or WSL with the `linux-gcc` preset. The build fetches gRPC
`v1.78.1` and generates the current Flower protobuf/gRPC sources into the build
tree from `framework/proto`. No generated files are copied into the source
tree.

```bash
cmake --preset linux-gcc
cmake --build --preset linux-gcc
```

The executable is `build/linux-gcc/flwr_client`. The `windows-msvc` preset is
provided for native Windows builds but has not been verified.

### Cross-compiling for ARM (NanoPI)

The presets are named after the target architecture:

- `linux-arm64-cross` — aarch64 Linux (64-bit ARMv8), via
  `cmake/toolchain-arm64.cmake`.
- `linux-armv7-cross` — 32-bit ARMv7 hard-float Linux (Cortex-A7, e.g. the
  original NanoPi boards), via `cmake/toolchain-armv7.cmake`.

> [!IMPORTANT]\
> The Cortex-A7 is a 32-bit ARMv7 CPU and cannot run aarch64 binaries. Use the
> `linux-armv7-cross` preset with an `arm-linux-gnueabihf` toolchain (Debian's
> `gcc-arm-linux-gnueabihf`, or the official Arm GNU Toolchain
> `arm-none-linux-gnueabihf`). Fedora's `gcc-arm-linux-gnu` package is the
> soft-float ABI and is not compatible with the NanoPi's hard-float rootfs.

Requirements for both cross presets:

- A GNU cross compiler for the target. Pass `-DCMAKE_C_COMPILER` and
  `-DCMAKE_CXX_COMPILER` to use other compilers.
- A target rootfs for the sysroot, passed as
  `-DFLWR_ARM64_SYSROOT=<dir>` or `-DFLWR_ARMV7_SYSROOT=<dir>`. The rootfs
  needs OpenSSL 3 dev files (`libssl-dev` on Debian/Armbian) and glibc dev
  files.
- Host `protoc` and `grpc_cpp_plugin` on `PATH`. They must match the bundled
  protobuf and gRPC `v1.78.1`, because the build runs them to generate code.
  Build them once with the native preset:

  ```bash
  cmake --preset linux-gcc
  cmake --build --preset linux-gcc --target protoc grpc_cpp_plugin
  export PATH="$PWD/build/linux-gcc/_deps/grpc-build:$PWD/build/linux-gcc/_deps/grpc-build/third_party/protobuf:$PATH"
  ```

```bash
cmake --preset linux-armv7-cross -DFLWR_ARMV7_SYSROOT=/path/to/nanopi-rootfs
cmake --build --preset linux-armv7-cross
```

The executable is `build/linux-armv7-cross/flwr_client`. Verify with
`file build/linux-armv7-cross/flwr_client` (expect "ELF 32-bit LSB
executable, ARM, EABI5").

> [!NOTE]\
> The cross build is not yet verified. gRPC builds its own `protoc` for the
> target during the build. If that step fails, use a host gRPC package or a
> different provider.

## Running the example

### 1. Start the SuperLink (remote machine)

```bash
flower-superlink --insecure --fleet-api-type grpc-rere --fleet-api-address 0.0.0.0:9092
```

### 2. Start the Python ServerApp (remote machine)

The existing `server.py` / `fedavg_cpp.py` from `examples/quickstart-cpp/` are
used as-is (legacy `Strategy` API, supported through Flower's compatibility
mode). The directory includes a `pyproject.toml` that registers `server.py` as
the Flower ServerApp. From the repository root, run:

```bash
flwr run examples/quickstart-cpp --stream
```

### 3. Start the C++ clients (NanoPI)

```bash
build/linux-gcc/flwr_client 0 127.0.0.1:9092
build/linux-gcc/flwr_client 1 127.0.0.1:9092
```

The first argument is the client ID (used to derive the node public key), the
second is the SuperLink Fleet API address.

> [!WARNING]\
> `--insecure` disables TLS. Use it only on a trusted development network.
> Production requires TLS and SuperNode authentication (Phase 4).

## How it works

The client implements the current Fleet lifecycle:

```text
open gRPC channel
register node (public key derived from client ID)
activate node
start heartbeat (30s interval)
repeat:
      PullMessages(Node)
      decode Message and RecordDict
      call Trainer (get_parameters / fit / evaluate)
      build reply Message with reply metadata
      PushMessages(Node, Message, ObjectTree)
until shutdown (SIGINT)
stop heartbeat
deactivate node
```

### Wire format (Profile A)

Parameters are exchanged as raw little-endian IEEE-754 doubles:

- `stype = "cpp_double"`
- `dtype = "float64"`
- `shape = [n]` (number of doubles)
- `data = n doubles in little-endian byte order`

This matches the legacy Python strategy in `examples/quickstart-cpp/fedavg_cpp.py`,
which decodes tensors with `struct.unpack("d", ...)`.

### Module layout

```text
embedded_cpp_client/
   CMakeLists.txt
   CMakePresets.json        # windows-msvc, linux-gcc, linux-arm64-cross,
                            # linux-armv7-cross
   cmake/
      toolchain-arm64.cmake # aarch64 (64-bit ARM) Linux cross-compile
      toolchain-armv7.cmake # ARMv7 hard-float Linux cross-compile (Cortex-A7)
   include/
      flower_client.h       # public lifecycle API
      flower_trainer.h      # model/training callback interface
      fleet_transport.h     # gRPC channel and Fleet stub wrapper
      heartbeat.h           # background heartbeat
      message_codec.h       # Message and RecordDict conversion
      parameter_codec.h     # array and parameter serialization
      trainer.h             # linear model trainer (stub for the ML library)
   src/
      main.cc               # argument parsing, runs FlowerClient
      flower_client.cc      # register, activate, poll, reply, shutdown
      fleet_transport.cc    # gRPC channel, Fleet RPCs (no retries yet)
      heartbeat.cc          # background heartbeat
      message_codec.cc      # Message and RecordDict conversion
      parameter_codec.cc    # ArrayRecord conversion
      trainer.cc            # linear model trainer
   proto/                   # generated at build time, not hand-maintained
```

## Status and next steps

Implemented (Phases 1–2):

- Current Fleet protobuf generation from `framework/proto` at build time
- Register / activate / heartbeat / poll / push / deactivate lifecycle
- `Message`/`RecordDict` decoding and encoding (Profile A, `cpp_double`)
- Reply metadata (`reply_to_message_id`, run/node metadata)
- Linear model trainer behind the `Trainer` interface

Follow-ups (Phases 3–5):

- Third-party ML library behind `Trainer`
- TLS and SuperNode authentication
- Object transfer for large models
- Persistent node identity and reconnect/backoff
- Modern `ServerApp` strategy compatibility (Profile B, `.npy` codec)