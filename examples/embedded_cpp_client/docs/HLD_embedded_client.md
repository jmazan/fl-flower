# Native Flower C++ client template for NanoPI

## Verdict

The example is not compatible with Flower 1.37.0 as-is. The model and callback layer are reusable, but the C++ Flower transport client is based on an obsolete protocol.

The target deployment is:

```text
NanoPI                                      Remote machine
                                               |
Native C++ Fleet client ------------ gRPC ---- Flower SuperLink
                                               |
                                      Python ServerApp / Strategy
```

For a trusted development network, the remote machine can expose the Fleet API with `grpc-rere`:

```bash
flower-superlink --insecure --fleet-api-type grpc-rere --fleet-api-address 0.0.0.0:9092
flwr run <server-app-directory> --stream
```

The second command starts the remote Python `ServerApp` from a current Flower app directory. The existing `server.py` and `fedavg_cpp.py` are used **as-is** — no Python or server-side changes are in scope for this deliverable. (Note: `flwr run` requires the ServerApp to live in a Flower app directory; setting that up is a deployment prerequisite handled on the server side, not part of the C++ client work.) `flower-server-app` was removed before Flower 1.37; the ServerApp is started with `flwr run` instead. The NanoPI client connects to `<remote-host>:9092`. These insecure commands are for a local proof of concept only. Production requires TLS and SuperNode authentication.

## Current status

Phases 1 and 2 are implemented in `examples/embedded_cpp_client/`. The changes
below were code-reviewed against the Flower 1.37 protos and the SuperLink
reply validation. They have not been compiled or run yet.

Corrections applied:

- Replies set `created_at` (unix seconds) and a `ttl` capped by the time left
  on the request, with a 1-second floor. The SuperLink
  (`validate_message(is_reply_message=True)`) rejects replies without these
  fields, so before this fix every reply was dropped and rounds timed out.
- Each RecordDict key appears once. This removes the duplicate `fitres.status`
  and `evaluateres.status` items and the one-item-per-metric output.
- `evaluateres.loss` is sent only as a MetricRecord double. The string `loss`
  under `metrics` is removed.
- The heartbeat sends its first beat right away, and `stop()` wakes the thread
  immediately instead of waiting up to 30 seconds.
- `FlowerClient::running_` is `std::atomic<bool>`, so it is safe to set from
  the SIGINT handler.
- `Reconnect` on pull and push responses is ignored. The Python grpc-rere
  client ignores it too. The SuperLink sets `reconnect=5` on every push
  response, so acting on it would re-register after every reply. The real
  reconnect instruction is `ServerMessage.reconnect_ins`, which the C++ client
  does not handle yet.
- CMake: when cross-compiling, `protoc` and `grpc_cpp_plugin` come from the
  host `PATH`. Presets: `windows-msvc`, `linux-gcc`, `linux-arm64-cross`
  (toolchain: `cmake/toolchain-arm64.cmake`).

Verified against the framework: the Fleet RPCs and message fields match the
1.37 protos; `--fleet-api-type grpc-rere` exists; the 30-second heartbeat is
inside the server's accepted range `[10, 1800]`.

Prerequisite for a live run: `flwr run` needs a Flower app directory with a
`pyproject.toml`. `examples/quickstart-cpp` has none, so one must be added on
the server side. This is outside the scope of the client work.

Open items:

- `examples/quickstart-cpp/fedavg_cpp.py` (line ~93) multiplies `num_examples`
  by `metrics["loss"]`. That string is no longer sent, so the legacy strategy
  needs `evaluate_res.num_examples * evaluate_res.loss`. It is out of scope and
  was not changed.
- Acting on `reconnect_ins` (re-register and backoff) is not implemented.
- Unverified: whether the server merges or overwrites duplicate RecordDict
  keys (the one-key fix is correct either way); whether the `client-<id>`
  public key is always accepted (the server rejects duplicates).
- Unverified: the cross build. gRPC v1.78.1 may build and run its own `protoc`
  target for the device architecture during the build. If so, the aarch64
  build fails and needs a host gRPC or a different provider.
- Not yet built or run. Phase 1 smoke test and the end-to-end run are pending.

Next steps (plan to one training round):

1. Build on WSL/Linux: `cmake --preset linux-gcc`, then
   `cmake --build --preset linux-gcc`. Fix any compile errors.
2. Run end-to-end: `flower-superlink --insecure --fleet-api-type grpc-rere`,
   a server app directory with `pyproject.toml` that runs `fedavg_cpp.py`, and
   two clients. Confirm that a round completes and that the server receives the
   returned weights.
3. Replace the linear model with the user's DNN/CNN/LSTM library behind
   `Trainer`. Map each tensor to an ArrayRecord with an agreed shape, and
   validate tensor count and byte length before training.
4. Cross-compile for NanoPI: `cmake --preset linux-arm64-cross` with the
   aarch64 GNU toolchain. Put the host `protoc` and `grpc_cpp_plugin` on
   `PATH`.

Steps 3 and 4 are future work and are outside the current scope.

## Scope and location

The deliverable is a **new standalone example** at `examples/embedded_cpp_client/` implementing a native C++ Fleet client. The old `examples/quickstart-cpp/` example is left untouched.

The first deliverable covers **Phase 1 (protocol smoke test)** and **Phase 2 (inline model exchange)** only. Phases 3–5 (trainer integration, production transport, modern strategy compatibility) are follow-ups and are described below for context, not for the first implementation.

## What still works

The Python server in `server.py` uses the legacy `Strategy` API, but Flower 1.37 still supports it through compatibility mode. `FedAvgCpp` can therefore remain largely unchanged for an initial demonstration. **No server-side or Python changes are required for the first deliverable.**

The model code in `line_fit_model.cc` and the methods in `simple_client.cc` are also conceptually suitable. They already isolate:

- receiving parameters
- training
- evaluating
- returning parameters and metrics

That is a good boundary for integrating a third-party NanoPI ML library later.

## Main incompatibilities

1. **Obsolete Fleet RPCs**

The C++ client calls:

- `CreateNode`
- `DeleteNode`
- `PullTaskIns`
- `PushTaskRes`

through `communicator.cc` and `grpc_rere.cc`.

Flower 1.37 exposes:

- `RegisterNode`
- `ActivateNode`
- `DeactivateNode`
- `UnregisterNode`
- `SendNodeHeartbeat`
- `PullMessages`
- `PushMessages`
- `GetRun`
- `GetFab`
- object transfer and acknowledgement RPCs

These are defined in the current `fleet.proto`. The old RPCs are no longer available.

2. **Obsolete payload model**

The C++ implementation exchanges legacy `Task` and `RecordSet` objects. Flower 1.37 exchanges `Message` objects containing `RecordDict`, `ArrayRecord`, `MetricRecord`, and `ConfigRecord`.

The old recordset.pb files are stale. The current protocol uses `recorddict.proto`.

3. **CMake is stale**

The example’s `CMakeLists.txt`:

- fetches Flower from unpinned `main`
- compiles generated protobuf files already checked into the SDK
- globs all generated `.cc` files
- does not generate the current Fleet dependencies
- uses gRPC `v1.43.2`

The framework CMake file also still references the removed `recordset.proto`. A clean current SDK build therefore needs restructuring, not just a version-number update.

The standalone template should generate protobuf and gRPC C++ sources into the build tree from `framework/proto`, using pinned versions. When cross-compiling for ARM, build `protoc` and `grpc_cpp_plugin` for the host and use them to generate sources for the NanoPI target. Do not copy generated files into the source tree or use source globs for the protocol target.

**Version pinning decision:** pin gRPC to `v1.78.1` (latest stable). The framework's Python dependency is `grpcio>=1.70.0` (`framework/pyproject.toml`); the gRPC wire protocol is stable across versions, so a newer C++ gRPC is compatible. Older gRPC versions do not build with modern toolchains: `v1.43.2` (legacy SDK) fails with GCC 15 (bundled abseil), and `v1.70.1` fails with CMake 4.x (bundled protobuf `utf8_range` `install(EXPORT)`). The actual fix is to stop fetching Flower from unpinned `main` and to generate the current protos from `framework/proto` instead of compiling the stale checked-in `.cc` files. If a newer gRPC is needed later, that is a separate, deliberate upgrade.

4. **ClientApp is not a native C++ process**

Flower 1.37’s `ClientApp` and `flower-client-app` workflow is Python-oriented. A native C++ executable cannot simply be substituted for a Python `ClientApp`. Under the NanoPI constraint, the C++ process must implement the current Fleet API and act as a SuperNode-compatible client itself.

## Native C++ migration scope

The native migration should preserve the trainer behavior in `SimpleFlwrClient` and replace the communication layer:

1. Generate the current protobuf files: Fleet, Message, RecordDict, Node, Heartbeat, Run, FAB, and related dependencies.
2. Rewrite the communicator lifecycle:
   - register/activate node
   - heartbeat
   - pull messages
   - push replies
   - deactivate/unregister
   - reconnect and retry
3. Translate Flower `Message`/`RecordDict` payloads into the existing `FitIns`, `EvaluateIns`, and `Parameters` C++ types.
4. Translate the existing C++ results back into `Message`/`RecordDict`.
5. Implement reply metadata, message IDs, object trees, acknowledgements, and object transfer.
6. Add TLS and SuperNode authentication for anything beyond a local demo.
7. Replace the current glob-based CMake setup with pinned, target-based protobuf/gRPC generation.

**First deliverable scope (Phases 1–2):** items 1, 2 (register/activate, heartbeat, pull, push, deactivate — without reconnect/retry beyond basic handling), 3, 4, and 7. Items 5 (object transfer), 6 (TLS/auth), and the reconnect/retry part of item 2 are follow-up phases.

For the current linear model, the first compatibility profile can use raw `double` buffers stored in `ArrayRecord` entries with `stype = "cpp_double"`. The existing Python strategy’s conversion logic can continue to decode them. A modern built-in `FedAvg` strategy expects NumPy `.npy` array payloads, so that profile requires a small `.npy` codec or a custom Python aggregation strategy.

This is a medium-sized transport adapter project. The training code itself should require little or no change.

## Recommended architecture for the first NanoPI demo

The NanoPI must run one native C++ process that implements the current Flower Fleet gRPC request-response client. Python remains on the remote machine for the Flower `ServerApp` and SuperLink:

```text
NanoPI                                      Remote machine
                                               |
Native C++ Flower Fleet client ---- gRPC ---- Flower SuperLink
                                               |
                                      Python ServerApp / Strategy
```

The C++ process does not need Python, `ClientApp`, or `flwr run`. It handles the Fleet lifecycle, current Flower protobufs, message dispatch, and the trainer. The trainer remains behind a Flower-independent interface:

```text
get_parameters()
fit(parameters, config)
evaluate(parameters, config)
```

This avoids coupling the ML library to Flower protobufs and makes it easy to replace the stubbed trainer later. A native Fleet client is a larger implementation than a Python bridge, but it is the required architecture when Python cannot run on the NanoPI.

The client should be split into these modules (new example at `examples/embedded_cpp_client/`):

```text
embedded_cpp_client/
   CMakeLists.txt
   CMakePresets.json        # windows-msvc, linux-gcc, linux-arm64-cross
   cmake/
      toolchain-arm64.cmake # aarch64 Linux cross-compile
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

**Node identity (first deliverable):** keep it simple — derive a stable identifier from the client ID argument (e.g., `client-<id>` or a hash of it) and use it as the `public_key` for `RegisterNode`/`ActivateNode`. No identity persistence or key management in the first version; that is part of Phase 4.

Keep the trainer independent from generated protobuf classes. Its public contract should be limited to ordinary C++ parameters, configuration, metrics, and results:

```cpp
class Trainer {
 public:
   virtual Parameters get_parameters() = 0;
   virtual FitResult fit(const Parameters&, const Config&) = 0;
   virtual EvaluateResult evaluate(const Parameters&, const Config&) = 0;
   virtual ~Trainer() = default;
};
```

The runtime loop is:

```text
open gRPC channel
register node with a stable public-key identifier
activate node
start heartbeat
repeat:
      PullMessages(Node)
      decode Message and RecordDict
      call Trainer
      build reply Message with reply metadata
      PushMessages(Node, Message, ObjectTree)
until shutdown (SIGINT)
stop heartbeat
deactivate node
unregister node when appropriate
```

Start with one message at a time and one trainer instance. Add concurrency only after retry, reconnect, and graceful shutdown are working.

## Message and parameter profiles

There are two useful server-side profiles.

### Profile A: smallest first demo

Keep the existing Python `FedAvgCpp` compatibility strategy and its raw `cpp_double` representation. The C++ adapter maps the current `Message` and `RecordDict` envelope to the logical operations `get_parameters`, `train`, `evaluate`, and `reconnect`.

For compatibility records, use the same shape as the legacy adapter, such as `fitins.parameters`, `fitins.config`, `fitres.parameters`, and `fitres.num_examples`. Store each tensor as an `Array` with `stype = "cpp_double"`, an explicit shape, and a documented byte order.

This is the recommended first path because it validates the native transport and NanoPI trainer without requiring a server rewrite.

### Profile B: modern built-in ServerApp strategies

Use a modern Python `ServerApp` and `flwr.serverapp.strategy.FedAvg`. Its default record keys are `arrays`, `config`, `metrics`, and `num-examples`.

The current Python `Array` implementation expects NumPy `.npy` serialization when `stype = "numpy.ndarray"`. Raw native `double` bytes with `stype = "cpp_double"` are not consumable by the built-in NumPy aggregation path. The C++ client must either implement a small NumPy `.npy` writer/reader for supported dtypes and shapes, or use a custom Python strategy that aggregates the documented `cpp_double` format.

Use Profile A first for communication and trainer integration, then move to Profile B when interoperability with built-in modern strategies is more important than the simplest codec.

## Fleet lifecycle details

The current Fleet API requires more than polling tasks:

1. **Node identity**: for the first deliverable, derive a stable identifier from the client ID argument (e.g., `client-<id>`) and carry it in `public_key`. No persistence or key management yet; that is Phase 4.
2. **Registration**: call `RegisterNode` once for a new identity.
3. **Activation**: call `ActivateNode` and retain the returned node ID.
4. **Heartbeat**: call `SendNodeHeartbeat` periodically while active. The current default interval is 30 seconds; use a conservative interval and account for network outages.
5. **Message polling**: call `PullMessages` with `Node { node_id }` and tolerate an empty response.
6. **Reply routing**: preserve the incoming message ID in `reply_to_message_id`, as well as the run and node metadata.
7. **Object handling**: support `ObjectTree`, `PullObject`, `PushObject`, and `ConfirmMessageReceived`. A small demo may keep payloads inline, but a production client must support object transfer for larger models.
8. **Shutdown**: deactivate the node and unregister it only when the identity is no longer intended to be reused.

When SuperNode authentication is enabled, registration and every Fleet request also require Flower’s current public-key/signature metadata and TLS. Implement that as a separate phase; a random identifier is not production authentication.

## Implementation phases

### Phase 1: protocol smoke test (first deliverable)

- Generate the current Fleet protobufs.
- Connect to the remote SuperLink with `--insecure`.
- Register, activate, heartbeat, poll, and cleanly deactivate a node.
- Add structured logging and bounded RPC timeouts.

### Phase 2: inline model exchange (first deliverable)

- Decode `Message` and `RecordDict`.
- Implement Profile A with the two `cpp_double` arrays used by the linear model.
- Return a stubbed update from `fit` and a deterministic loss from `evaluate`.
- Run two NanoPI clients against the existing Python strategy.

### Phase 3: trainer integration (future work, not in current scope)

- Replace the stub with the third-party ML library behind `Trainer`.
- Validate tensor count, dtype, shape, and byte length before calling the library.
- Add round-trip parameter tests and malformed-message tests.

### Phase 4: production transport (follow-up)

- Add TLS and SuperNode authentication.
- Implement object transfer for large models.
- Persist node identity and recover after process restarts.
- Add reconnect backoff, heartbeat failure handling, and watchdog behavior.

### Phase 5: modern strategy compatibility (follow-up)

- Switch to Profile B if desired.
- Add NumPy `.npy` serialization or a server-side custom codec.
- Replace the compatibility `FedAvgCpp` strategy with the modern `ServerApp` strategy.

## Run modes (first deliverable)

There are two ways to run the Flower server side:

- `flower-superlink --insecure --fleet-api-type grpc-rere --fleet-api-address 0.0.0.0:9092` — starts the SuperLink with the gRPC-rere Fleet API that the C++ client connects to.
- `flwr run <server-app-directory> --stream` — starts the Python `ServerApp` (the existing `server.py` / `fedavg_cpp.py`), which connects to the SuperLink.

**Decision:** the first deliverable targets the `flower-superlink` + `flwr run` combination, since the C++ client always talks to the SuperLink’s Fleet API regardless of how the ServerApp is launched. The client itself is agnostic to the server-side launch mode; supporting `flwr run`-only orchestration later (e.g., SuperLink auto-start) must not block this design.

## Additional issues worth fixing

- Resolved: the `data_indices` initialization bug in the trainer is fixed.
- Resolved: `set_parameters` checks payload sizes before reading them.
- Open: the `cpp_double` wire format is raw native `double` bytes, so it relies
  on a little-endian host. This holds on x86 and aarch64, but nothing enforces
  it. Use an explicit encoding if a big-endian architecture must be supported.
- Resolved: the README launch instructions separate the remote SuperLink from
  the native client.
- Resolved: the client ID is used to derive the node public key.

## Recommendation

Build a standalone native C++ Fleet client template in a new `examples/embedded_cpp_client/` directory. Implement the current Fleet lifecycle and `Message`/`RecordDict` envelope (Phases 1–2), then use the existing `FedAvgCpp` strategy as a compatibility profile for the first remote-server demo. Keep the trainer behind a narrow C++ interface so the stub can be replaced by the NanoPI ML library without changing the Flower transport. No Python or server-side changes are in scope for the first deliverable.

Use insecure gRPC only on a trusted development network. Add TLS, SuperNode authentication, persistent node identity, object transfer, and reconnect handling before production deployment.

## Memory usage analysis

Memory analysis script is at examples/embedded_cpp_client/tools/map_module_size.awk

Usage:

    cd examples/embedded_cpp_client
    awk -f tools/map_module_size.awk build/flwr_client.map 2>totals.txt | sort -t$'\t' -k4,4nr

stdout gets rows of module \t image_bytes \t ram_bytes \t total_bytes; the header and grand totals go to stderr (totals.txt above). "image" = bytes stored in the program image = flash on an MCU target; "ram" = bytes resident at runtime (.data + .bss + writable tables).

Validation (this binary, build/flwr_client):

    script TOTAL   image=33,394,446  ram=390,040
    size           text+data=33,394,446  data+bss=390,040   -> exact match

I also checked every output section's declared size against the sum of its input-section records (all zero-gap) and cross-checked classification with readelf -SW section flags (WA vs A).

Top consumers:

    module                        image      ram     total
    libgrpc.a                 23,360,040  264,384  23,624,424
    libprotobuf.a              4,887,965   35,949   4,923,914
    (linker-synthesized)       1,277,251    4,744   1,281,995
    libflwr_proto.a              758,619   31,805     790,424
    libre2.a                     505,201   12,525     517,726
    flwr_client (own code)       258,147       32     258,179
    libcares.a                   219,224      816     220,040
    (fill/alignment)              82,326   13,994      96,320
    ... 99 modules total

Three quirks of GNU ld map files the script has to handle (each of which silently corrupts the numbers if you skip it):

1. Only parse after the "Linker script and memory map" header — the archive table and "Discarded input sections" before it contain the same file.o line shapes.
2. Orphan output sections are printed at column 0 WITHOUT a leading dot (protodesc_cold 0x... 0x6a46, malloc_hook, flags_help_cold); if you only recognize .name headers, their bytes get absorbed into .rodata/.text (that cost 29,920 B of misattribution here). The script inherits their RAM/flash class from the neighboring section, which matches how ld places orphans (verified: malloc_hook has AX, protodesc_cold A, linkarr_upb_AllExts WAR flags).
3. Linker-synthesized sections (.plt, .dynsym, .rela.*, .eh_frame_hdr, .dynamic, …) are attributed to whichever object ld lists first — here that made crt1.o look like 1.3 MB. They're regrouped under the pseudo-module (linker-synthesized).

Memory-classification rules (by output-section name): .bss/.tbss/COMMON → ram only; .data/.got/.init_array/.tdata/.dynamic → both image and ram; code/rodata/eh_frame/plt → image only; .debug/.comment/.stab/.annobin/.gnu.build.attributes → excluded (not loaded). On the arm64/MCU build the same script works — "image" is then literally flash.

Two side notes: this run's classification was tuned against readelf, so residuals are exactly 0 on this binary; and if you want symbol-level (not module-level) detail, nm --print-size --size-sort -C build/flwr_client is the complement. The linux-gcc preset rebuild is still running in the background; its map will be at build/linux-gcc/flwr_client.map and this script applies to it unchanged.

Exact breakdown of build/flwr_client (72,455,496 bytes):

    loadable into memory (what the awk TOTAL counts):
      code + rodata + data (in file)   33,394,446   <- awk "image"
      .bss (zero-fill, not in file)       56,344   <- awk "ram" part
    never loaded, file-only metadata:
      .strtab (symbol names)           33,779,170   32.2 MiB
      .symtab (symbol table)            5,267,664    5.0 MiB
      .debug_*, .comment, .annobin, etc      ~2,400
      .shstrtab + section headers            3,526
    ELF/program headers + segment alignment  ~9,300
                                          -----------
      total                             72,455,496  ✓

So ~39 MB of the file is just the symbol table and its string table — normal for an unstripped C++ binary (that's where all the long _ZN4absl... names live). strip build/flwr_client would bring the file to ~33.5 MB with zero change to runtime memory; for the embedded/flash case what matters is the loadable image, i.e. the awk numbers.

Two clarifications on the numbers you saw:
- awk's grand TOTAL line is image=33,394,446  ram=390,040 (two columns, not one 23 MB figure; image and ram overlap on .data, so don't add them).
- 23 MB was the row for libgrpc.a alone (23,624,424) — gRPC core is ~70% of the whole image.


Please review the @_; phase 1 and 2 are already implemented. I want to run phase 3 but for that I would like to use genetic algorithm as implemented by @_. 
1) analyse the current input configuration of features as defined in @_ and let me know if this feature list shall be extended with some features which are currently fetched into decider LLM context.
2) propose which fundamental features shall be evaluated using genetic algorithm.
3) shall we evaluate (run genetic search) on technical and fundamental features separately or shall I create on combined feature list and dataset.
4) how about target label (fitness function), propose some which would be aligned with the task of decider LLM.
Propose a detailed plan for phase 3 stage 1, but do not make any changes.

Measured both builds. One important discovery first: the build/ tree you've been looking at was configured with no build type at all (CMAKE_BUILD_TYPE empty in its CMakeCache), so it's a plain -O0 build — its 33 MB image is the worst case, not an optimized figure.

The properly optimized build (build/linux-gcc, CMake Release = -O3 -DNDEBUG, no -g — debug sections are 1.6 KB of stubs) finished, and the awk script validated exactly against size on it again:

    optimized (-O3, no debug)          x86-64 measured
    loadable image (flash)             15,733,211 B  = 15.0 MiB
    static RAM (.data+.bss)                340,660 B  = 333 KiB
    on-disk binary                      21.1 MB unstripped
    after strip                         15,744,104 B  = 15.0 MiB
    measured peak RSS (startup→register)     29.0 MiB

so the ballpark you asked for is ~15 MiB image, ~330 KiB static RAM, ~30 MiB RSS early on. Your earlier 33 MB/72 MB numbers were the unoptimized build plus symbol table (.strtab alone was 32 MB; strip removes it and it never loads).

Where the 15 MiB goes (-O3, from the script on the new map):

    libgrpc.a          10,361,932   (66%)
    libprotobuf.a       1,938,734
    flwr_client (own)   1,107,131
    libflwr_proto.a       491,013
    libre2.a              377,919
    (linker-synthesized)  284,871
    ... 98 modules, TOTAL 15,733,211 image / 340,660 ram

Verdict for the NanoPi with 150 MB: the binary is not the problem — nothing in the code/image side must be optimized. Ballpark steady state is 30–45 MiB RSS: ~333 KiB static data, ~25 MiB heap (measured: gRPC channel + TLS/ECDSA + datasets), ~15 MiB of file-backed text/rodata — and file-backed pages are shared with the page cache and reclaimable under pressure. Even if every text page gets touched you stay under ~45 MiB.

What actually scales against your 150 MB is heap data, not code:
- parameter_codec serializes whole model tensors into protobuf messages and std::strings — several transient copies of the full parameter vector per round. Budget roughly 2–4× model size at peak.
- Datasets grow with sample count (the current synthetic 1000/100/500 samples is ~15 KB — irrelevant; real per-node data isn't).

So budget: 150 MB − (~45 MB runtime floor) ≈ 100 MB for model parameters + data + gRPC traffic buffers, and you're fine.

Notes before you cross compile:
- -O2 specifically: this measurement is -O3 (CMake Release). -O2 lands within a few % of these numbers (often slightly smaller); if flash/size ever matters, -Os (MINSIZEREL) is the lever. To force exactly -O2: -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG".
- aarch64 code size is typically within ±10% of these x86-64 figures — same ballpark, confirm on-device with the same script.
- Blocker on this host: no ARM cross compiler installed — aarch64-linux-gnu-g++ is not on PATH (the toolchain file expects it), and the cross preset also needs host protoc + grpc_cpp_plugin on PATH. You'll need g++-aarch64-linux-gnu (or the Fedora gcc-c++ aarch64 cross package) before cmake --preset linux-arm64-cross will configure.
- The -O3 map file is at build/linux-gcc/flwr_client.map (23 MB); awk -f tools/map_module_size.awk works on it unchanged.
