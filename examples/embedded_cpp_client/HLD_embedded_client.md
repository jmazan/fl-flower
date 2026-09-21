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
   include/
      flower_client.h       # public lifecycle API
      flower_trainer.h      # model/training callback interface
      parameter_codec.h     # array and parameter serialization
   src/
      flower_client.cc      # register, activate, poll, reply, shutdown
      fleet_transport.cc    # gRPC channel, Fleet stub, retries
      heartbeat.cc          # background heartbeat
      message_codec.cc      # Message and RecordDict conversion
      parameter_codec.cc    # ArrayRecord conversion
   proto/                  # generated at build time, not hand-maintained
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
until shutdown or reconnect instruction
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

### Phase 3: trainer integration (follow-up)

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

- `train_SGD` initializes `data_indices` with `dataset.size()` zeroes and then appends the real indices, producing an incorrect index array.
- Raw doubles are serialized using native endianness in both C++ and Python. Use an explicitly defined format if NanoPI and server architectures may differ.
- `set_parameters` assumes exactly two tensors and can dereference an invalid iterator if the payload is malformed.
- The launch instructions in `README.md` describe the old C++ workflow and should be updated to launch the remote Python server separately from the native NanoPI client.
- The client ID is accepted by the example but is not meaningfully used by the transport. Node identity should be managed by the new Fleet client (derived from the client ID in the first deliverable).

## Recommendation

Build a standalone native C++ Fleet client template in a new `examples/embedded_cpp_client/` directory. Implement the current Fleet lifecycle and `Message`/`RecordDict` envelope (Phases 1–2), then use the existing `FedAvgCpp` strategy as a compatibility profile for the first remote-server demo. Keep the trainer behind a narrow C++ interface so the stub can be replaced by the NanoPI ML library without changing the Flower transport. No Python or server-side changes are in scope for the first deliverable.

Use insecure gRPC only on a trusted development network. Add TLS, SuperNode authentication, persistent node identity, object transfer, and reconnect handling before production deployment.