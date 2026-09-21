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

The build fetches gRPC `v1.78.1` (pinned, latest stable; the framework's
Python dependency is `grpcio>=1.70.0`) and generates the current Flower
protobuf/gRPC sources into the build tree from `framework/proto`. No generated
files are copied into the source tree.

```bash
cmake -S . -B build
cmake --build build
```

The executable is `build/flwr_client`.

### Cross-compiling for ARM (NanoPI)

When cross-compiling, `protoc` and `grpc_cpp_plugin` must be built for the
host and made available via `CMAKE_PROGRAM_PATH` (or `PATH`). The generated
sources are then compiled for the ARM target:

```bash
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/arm-toolchain.cmake \
  -DCMAKE_PROGRAM_PATH=/path/to/host-bin
cmake --build build-arm
```

## Running the example

### 1. Start the SuperLink (remote machine)

```bash
flower-superlink --insecure --fleet-api-type grpc-rere --fleet-api-address 0.0.0.0:9092
```

### 2. Start the Python ServerApp (remote machine)

The existing `server.py` / `fedavg_cpp.py` from `examples/quickstart-cpp/` are
used as-is (legacy `Strategy` API, supported through Flower's compatibility
mode). They must live in a Flower app directory for `flwr run`:

```bash
flwr run <server-app-directory> --stream
```

### 3. Start the C++ clients (NanoPI)

```bash
build/flwr_client 0 127.0.0.1:9092
build/flwr_client 1 127.0.0.1:9092
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
   include/
      flower_client.h       # public lifecycle API
      flower_trainer.h      # model/training callback interface
      trainer.h             # linear model trainer (stub for Phase 3)
      parameter_codec.h     # array and parameter serialization
      message_codec.h       # Message and RecordDict conversion
      fleet_transport.h     # gRPC channel and Fleet stub
      hearthbeat.h          # background heartbeat
   src/
      main.cc
      flower_client.cc      # register, activate, poll, reply, shutdown
      fleet_transport.cc    # gRPC channel, Fleet stub
      hearthbeat.cc         # background heartbeat
      message_codec.cc      # Message and RecordDict conversion
      parameter_codec.cc    # ArrayRecord conversion
      trainer.cc            # linear model trainer
   proto/                  # generated at build time, not hand-maintained
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