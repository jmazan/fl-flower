# Review and plan: embedded C++ Flower Fleet client

Scope: `examples/embedded_cpp_client` (C++ Fleet gRPC client for a remote Flower 1.37 SuperLink).
Goal: C++ client on a remote device that pulls weights, runs one local training round, and pushes updated weights back.

## 1. Review findings

### Critical (blocks a round from completing)

1. **Replies are dropped by the SuperLink (`created_at` not set).**
   - `message_codec.cc` builds replies at lines ~257, ~309, ~361 with `run_id`, `src/dst_node_id`, `reply_to_message_id`, `group_id`, `ttl` (copied from the incoming message), and `message_type`. It never sets `created_at`.
   - Server `validate_message(is_reply_message=True)` (`server/utils/validator.py`) requires `created_at >= 1740700800` and `created_at + ttl > now`. With `created_at = 0`, both fail. The reply is logged as an error and `store_message_res` returns `None`. The round then times out.
   - Python's own `Message.create_reply` sets `created_at=now` and caps `ttl` to the remaining TTL of the instruction (`_limit_reply_ttl`).
   - Fix: set `created_at = now()` (unix seconds, double) and `ttl = ins.ttl - (now - ins.created_at)`, clamped to >= a small positive value. Add a test that asserts the fields.

2. **Evaluate metrics are sent as strings, which breaks the legacy aggregator.**
   - `trainer.cc:310` sets `result.metrics["loss"] = std::to_string(...)`. `message_codec.cc:357` forwards it through `add_config_string_value` into `evaluateres.metrics` (a ConfigRecord string).
   - `examples/quickstart-cpp/fedavg_cpp.py:93` does `num_examples * evaluate_res.metrics["loss"]`, which expects a number.
   - Fix: send the loss as the MetricRecord `evaluateres.loss` (already done at line 351) and stop emitting `loss` in `evaluateres.metrics`. Make the compat layer's read path match.

### High

3. **Duplicate RecordDict keys.** `add_config_sint64` and `add_config_string` each append a new item with the same key (for example `fitres.status`, `getparametersres.status`, `evaluateres.status`). `add_config_string_value` adds one item per metric under `fitres.metrics`. The Python parser's behavior (overwrite, first-wins, or error) is not verified. Fix: one ConfigRecord per key, and a helper that merges values.
4. **Shutdown can hang up to 30 s.** `heartbeat.cc` sleeps the full interval before its first beat, and `stop()` joins that thread. Fix: wait on a condition variable so `stop()` wakes it immediately.
5. **`running_` is `volatile bool`, written from a signal handler.** Fix: `std::sig_atomic_t` for the handler flag, or `std::atomic<bool>` set from the main loop.
6. **Reconnect and empty responses are not handled.** `PullMessages` and `PushMessages` responses have a `reconnect` field (`Reconnect { uint64 reconnect }`). The loop ignores it. Fix: on `reconnect`, re-run register/activate after a backoff.

### Medium

7. **Cross-compilation of protobuf is broken.** `CMakeLists.txt` always sets `_PROTOBUF_PROTOC` to `$<TARGET_FILE:protoc>`, which is a host-architecture binary when building for ARM. Only `grpc_cpp_plugin` honors `CMAKE_CROSSCOMPILING`. The README's `CMAKE_PROGRAM_PATH` flow is not implemented. Fix: use a host `protoc` when cross-compiling, and verify with a real ARM build.
8. **Misleading CMake comment.** "Order matters: dependencies first" is wrong. `protoc` resolves imports with `--proto_path`, and each generation rule depends only on its own `.proto`.
9. **Dead code in `trainer.cc`.** `SyntheticDataset` and `LinearModelTrainer` construct a `std::uniform_int_distribution<>` with double arguments and never use it. Remove it.
10. **Reported training loss uses only the training batch.** `compute_mse` covers the training batch only. Document it or compute loss over the full local set.
11. **Reply metadata lacks `message_id` handling.** Server-set IDs are assumed; confirm that an empty `message_id` on replies is correct (the Python client also leaves it empty and the SuperLink fills it in). Add a note to the HLD.
12. **Full gRPC v1.78.1 via FetchContent.** The build is slow and has not been verified in this environment. Consider a system gRPC option for embedded builds.

### Documentation and consistency

13. **HLD module layout is out of date.** It omits `main.cc`, `trainer.h/.cc`, `message_codec.h/.cc`, `heartbeat.h`. README has a typo (`hearthbeat.h`).
14. **HLD says `fleet_transport.cc` retries.** There are no retries in the code. Either add them (Phase 4) or correct the HLD.
15. **HLD says `flwr run` works with the legacy files as-is.** `examples/quickstart-cpp` has no `pyproject.toml` with `[tool.flwr.app]`, so `flwr run` cannot start it as shipped. Needs a decision (see Questions).
16. **`ServerApp` import path.** `server.py` uses `fl.server.ServerApp` / `fl.server.ServerConfig`. Python 1.37 exposes `flwr.serverapp.ServerApp` (`run_serverapp.py`). The compat path exists, but this should be tested, not assumed.

### Verified (no change needed)

- Fleet RPCs and message fields in `fleet.proto`, `message.proto`, and `recorddict.proto` match the C++ calls.
- `flower-superlink --fleet-api-type grpc-rere` exists (`superlink/cli/flower_superlink.py:839`, `TRANSPORT_TYPE_GRPC_RERE = "grpc-rere"`).
- Heartbeat bounds are `[10, 1800]` (`common/constant.py:48-49`). The client's 30 s interval is valid.
- `ActivateNode` and `SendNodeHeartbeat` validate the interval server-side (`fleet/message_handler/message_handler.py:341`).
- `public_key` is a bytes field that must be unique per node. The client's `"client-" + id` is accepted, but it is not persisted, so a restart with the same ID is fine only if no other node reuses the key.

## 2. Plan

### Phase A: correctness fixes to the existing client (no protocol change)
- A1. Reply metadata: set `created_at` and computed `ttl` in all three reply builders. Factor into one helper.
- A2. RecordDict: one ConfigRecord per key; merge helper for `*.status` and `*.metrics`.
- A3. Evaluate: remove `loss` from `evaluateres.metrics`; keep `evaluateres.loss` as MetricRecord.
- A4. Heartbeat: condition-variable wait; `stop()` returns immediately; first beat sent right after activate.
- A5. Signal flag: `std::atomic<bool>` or `sig_atomic_t`.
- A6. Remove dead `uniform_int_distribution` code in `trainer.cc`.
- A7. Handle `Reconnect` in pull/push responses (re-register and re-activate after backoff).

Verification: unit tests for the codec (metadata fields, record keys, evaluate shape) run in the existing test setup; one loopback test against a local SuperLink.

### Phase B: build and cross-compile
- B1. Fix `_PROTOBUF_PROTOC` for cross builds; fix the CMake comment.
- B2. Verify a host build (`cmake --build`) and a cross build for the NanoPI target, or document the exact toolchain.

### Phase C: end-to-end with the legacy Python server
- C1. Add a `pyproject.toml` for `flwr run` (or document `flower-superlink` + `flwr run` against a checked-in app dir). Requires decision.
- C2. Run `flower-superlink --insecure --fleet-api-type grpc-rere --fleet-api-address 0.0.0.0:9092` on the remote host.
- C3. Run `flwr run <app-dir> --stream`; run the C++ client on the device; confirm 3 rounds complete and aggregate losses are numeric.
- C4. Record results (round count, aggregate loss) in the README.

### Phase D: integration with the user's C++ ML project
- D1. Replace `LinearModelTrainer` with an adapter around the user's model library (DNN/CNN/LSTM). Keep the `Trainer` interface (`fit`, `evaluate`, `get_parameters`).
- D2. Define the weight layout contract (ordered tensors, shapes, dtype) and keep Profile A (`cpp_double`) or move to a typed Profile B.
- D3. Add shape and count validation on set_parameters; reject mismatches without corrupting local state.

### Phase E: HLD and docs
- E1. Update `HLD_embedded_client.md`: module layout (item 13), retries statement (item 14), `flwr run` prerequisite (item 15), phases A-D above, and the verified protocol facts.
- E2. Update `README.md`: module layout typo, cross-compile instructions, run steps.

## 3. Open questions for the user
1. Should Phase A (the correctness fixes, including the reply `created_at` fix) be done now, before anything else?
2. Should I add a `pyproject.toml` to `examples/quickstart-cpp` so `flwr run` works as shipped, or should the HLD document a separate app directory?
3. For Phase D, which model library and weight format does the user's project use? (This decides whether to keep Profile A or define Profile B.)
