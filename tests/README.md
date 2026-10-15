# DR_EVT Test Suite

This page is the source of truth for test categories, runner commands, and
test counts. The [Testing Guide](../docs/TESTING_GUIDE.md) explains the
validation methodology.

## Build and run

Configure, build, and install before invoking the shell runners:

```bash
export CMAKE_INSTALL_PREFIX="${PWD}/install"
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}" \
  -DDR_EVT_WITH_UNIT_TESTING=ON
cmake --build build -j4
cmake --install build
```

Run the ordinary CTest tests enabled by the current configuration with:

```bash
ctest --test-dir build --output-on-failure
```

The focused runners below exercise fixture suites and comparisons that are not
all registered with CTest:

```bash
./tests/run_scheduler_correctness_tests.sh
./tests/run_custom_scheduler_tests.sh
./tests/run_fcfs_queue_implementation_tests.sh --correctness
./tests/run_column_alias_tests.sh
./tests/run_time_mode_tests.sh
./tests/run_unit_tests.sh
./tests/run_feature_tests.sh
./tests/run_scale_tests.sh
./tests/run_easy_vs_conservative_correctness_tests.sh
./tests/compare_cpp_python_conservative.sh
./tests/run_replay_tests.sh
./tests/run_resource_history_tests.sh
./tests/run_job_store_tests.sh
./tests/run_redis_tests.sh
./tests/run_redis_grpc_client_test.sh
./tests/run_append_job_tests.sh
./tests/run_progressive_load_tests.sh
./tests/run_configs_tests.sh
./tests/run_python_tests.sh
./tests/run_warm_start_validation_tests.sh
./tests/run_max_time_tests.sh
./tests/run_grpc_tests.sh
./tests/run_backfill_window_grpc_test.sh
python3 tests/test_grpc_single_coordinator.py \
  "${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server"
```

Every shell runner ends with one machine-readable, ANSI-free status line:

```text
<<<<<<<<<<<<<<<< TEST RESULT: PASS | run_unit_tests >>>>>>>>>>>>>>>>
<<<<<<<<<<<<<<<< TEST RESULT: FAIL | run_unit_tests | exit=1 >>>>>>>>>>>>>>>>
<<<<<<<<<<<<<<<< TEST RESULT: SKIP | run_configs_tests | simulator was built without Protobuf support >>>>>>>>>>>>>>>>
```

Detailed per-case output remains above that line. The shared reporter also
covers early failures and runs each suite's temporary-file cleanup first.

Python-driven shell runners source `select_python.sh`. Unless
`PYTHON_EXECUTABLE` is set explicitly, it probes both `python` and `python3`
(plus available versioned commands) and selects the newest compatible
interpreter by its reported version. Binding runners search the configured
install library directory and both `lib/python` and `lib64/python`.

Some runners require build options or external packages for Python bindings,
Protobuf, gRPC, or MPI. Tests for unavailable optional features are not built
or are reported as skipped.

The Redis runner requires `redis-server`, `redis-cli`, and a simulator built
with `-DDR_EVT_WITH_REDIS=ON`. It starts and stops its own loopback-only Redis
server; no pre-existing Redis service is used.

## Inventory

| Category | Count | Runner or registration | Coverage |
|---|---:|---|---|
| Scheduler correctness | 34 | `run_scheduler_correctness_tests.sh` | C++/Python schedule and resource-trace consistency |
| Custom FCFS | 8 | `run_custom_scheduler_tests.sh` | Six focused API checks plus two golden schedules, including warm-start accounting and 2,000 jobs |
| Queue implementation differential | 34 × 4 | `run_fcfs_queue_implementation_tests.sh --correctness` | Equivalent schedules across deque, multimap, block, and circular queues |
| Column aliases | 8 | `run_column_alias_tests.sh` | Accepted runtime-column aliases and missing-column rejection |
| Run-time mode | 7 | `run_time_mode_tests.sh` | Actual, limit, distribution, capping, and planning behavior |
| Unit | 7 | `run_unit_tests.sh` | Basic parsing, formats, and execution |
| Feature | 8 | `run_feature_tests.sh` | Policies, modes, rejection, output formats, time-varying capacity, and replay-based warm start |
| Scale | 7 | `run_scale_tests.sh` | Workloads from 10 to 10,000 jobs |
| Conservative backfilling | 2 | two conservative runners above | Behavioral and C++/Python comparisons |
| Replay | 5 | `run_replay_tests.sh` | Resource equivalence and reclamation safety |
| Resource history | 5 | `run_resource_history_tests.sh` | Circular-buffer output and capacity handling |
| Job store | 6 | `run_job_store_tests.sh` | Capacity, growth/abort, reclamation, and statistics |
| Redis output | 1 integration runner | `run_redis_tests.sh` | Isolated server startup, CSV/hash output, time/resource indexes, namespace replacement, finalized-versus-unfinished visibility, pipelined bulk lookup, and byte-identical job and resource outputs for Redis/file runs of 200 jobs. Redis coverage runs with and without Ser20; the checkpoint portion runs only in the Ser20-enabled configuration. |
| Redis gRPC example | 1 integration runner | `run_redis_grpc_client_test.sh` | Batch append, advance, pipelined finalized-job lookup, one server fallback query, and original-order merged reporting |
| Append-job | 28 | `run_append_job_tests.sh` | 23 in-process C++ checks plus 5 optional gRPC checks, including integral streaming-limit validation, known actual runtimes, capacity-aware instantaneous/aggregate utilization, warm start, and validation |
| Checkpoint/restart | 7 groups | CTest (`test_checkpoint_restart`) and `run_redis_tests.sh` | Exact scheduler continuation plus byte-identical file, progressive-loading, and Redis job/resource output after archive-and-stitch recovery. |
| Progressive loading | 16 | `run_progressive_load_tests.sh` | 11 C++ checks plus 5 CLI checks for multi-file loading, bounded storage, block-queue integration, and memory checks |
| Protobuf configuration | 14 | `run_configs_tests.sh` | Configuration/CLI parity, capacity/simulation-start-time validation, and documented examples |
| Python API | 19 | `run_python_tests.sh` | Bindings, callbacks, streaming, checkpoint/restart, monitoring, policy APIs, and warm-start execution |
| gRPC client/server | 2 | `run_grpc_tests.sh` | Single-pair and optional MPI multi-server behavior |
| Multi-cluster dispatch | 2 | CTest (`test_python_performance_dispatch`, `test_mpi_performance_dispatch`) | Python/gRPC policy and native MPI coverage for app/workload sampling, CPU/GPU compatibility, runtime scaling, Turnaround, RelPerfOnly, and WaitTimeOnly placement, missing-prediction wait fallback and audit flags, adapted-limit and actual-duration wall-time policies, evaluation metrics, machine-size eligibility, and oversized-request truncation |
| Backfill-window gRPC | 5 repeated checks | `run_backfill_window_grpc_test.sh` | Focused rerun of the gRPC streaming binary; one check targets the backfill window |
| Single-coordinator gRPC | 1 | `test_grpc_single_coordinator.py` | Synchronized independent simulation servers |
| Queue input schema | 1 binary | CTest or installed `test_queue_input` | Legacy queue names or numeric queue IDs, plus accepted and rejected replay/simulation runtime invariants |
| Ser20-disabled serialization | 2 binaries | `t_state_rngen` and `t_state` | Native state serialization without Ser20 |
| Trace tools | 3 | `test_trace_tools.py` via CTest | Capacity inference, simulator-format conversion, direct schedule loading, and warm-start boundary/output behavior |
| Maximum time | 3 CLI cases | `run_max_time_tests.sh` | Inclusive cutoff behavior in simulation, replay, and warm-start execution |
| Warm start | 1 native binary + 9 CLI cases | CTest (`test_warm_start`, `test_warm_start_validation`) | Boundary classification, two-stage execution, runtime modes, capacity transitions, policies/queues, accounting/output, numeric/ISO simulation-start times, zero-start replay, inclusive maximum time, per-file timestamp encoding, and invalid configurations |
| Native CTest | 17, plus 1 with MPI | CTest | RNG and binary serialization, exact checkpoint/restart, trace policies, replay reclamation, custom scheduling, append/streaming APIs, maximum-time and warm-start coverage, capacity parsing, queue implementations, and CLI dispatch; CTest also registers the Python trace-tools test |

The gRPC portion of the append-job runner is skipped when gRPC support was not
built. The backfill-window runner executes the same five-check gRPC test binary
as the append-job runner, so it is a focused rerun rather than five additional
unique checks. Counts describe the checks performed by each runner; the native
CTest and focused-runner rows intentionally overlap.

## Fixture locations

- `test_traces/scheduler_correctness/`: small FCFS/EASY comparison fixtures;
- `test_traces/unit/`: parsing and basic execution fixtures;
- `test_traces/feature/`: policy, replay, and buffer fixtures;
- `test_traces/tools/`: capacity-analysis and warm-start fixtures;
- `test_traces/scale/`: larger workloads; and
- `test_configs/`: Protobuf configuration fixtures.

The scheduler-correctness layout is described in its
[fixture README](test_traces/scheduler_correctness/README.md). Input and output
schemas are defined in [Input Trace Files](../docs/user-guide/trace-formats.md)
and [Output Trace Files](../docs/user-guide/output-traces.md).

## Notable suites

### Scheduler-correctness tests

Scheduler-correctness fixtures compare both the scheduled-job trace and the
resource trace with outputs from `scripts/python_reference_scheduler.py`.
These comparisons test consistency between implementations, not independent
proof of the algorithm.

### Replay tests

Replay tests contain four simulation/replay CLI comparisons and the
`test_replay_reclamation` binary. The latter covers explicit, periodic, and
capacity-triggered flushing; final output; equal-time departures;
out-of-order completion; front-prefix blocking; and exactly-once output. The
CLI coverage also verifies the default and explicitly enabled replay report
sets, including byte-for-byte report contents.

Run the complete replay suite under Valgrind with:

```bash
./tests/run_replay_tests.sh --valgrind
```

### Progressive-loading tests

Progressive-loading tests exercise `--infile_list`, `Trace::load_next_file()`,
and `Simulation::run_progressive()`. Buffer behavior is documented in
[Output-Trace Buffers](../docs/dev/OUTPUT_TRACE_BUFFERS.md), and streaming API
semantics in the [C++ Streaming API](../docs/api/STREAMING_API.md).

### Wait-queue tests

```bash
./tests/run_fcfs_queue_implementation_tests.sh --correctness
./tests/benchmark_block_sizes.sh
${CMAKE_INSTALL_PREFIX}/bin/tests/test_block_queue
```

The [queue implementation runner](run_fcfs_queue_implementation_tests.sh)
compares the
scheduled-job and resource traces produced by the `circular`, `deque`,
`multimap`, and `block` FCFS queues. [`test_block_queue.cpp`](test_block_queue.cpp)
directly exercises the block queue at every supported block size.
[`benchmark_block_sizes.sh`](benchmark_block_sizes.sh)
runs the end-to-end performance and output-equivalence benchmark documented in
[Wait Queues](../docs/dev/WAIT_QUEUES.md#benchmark-record). Its workload is
[`test_traces/scale/huge_10000jobs.csv`](test_traces/scale/huge_10000jobs.csv);
it byte-compares every C++ queue output with `deque` and compares the normalized
Python EASY schedule with `deque` using a `0.001` time tolerance. The
differential runner uses the
[`scheduler_correctness`](test_traces/scheduler_correctness/) fixtures.

### Streaming API tests

```bash
./tests/run_append_job_tests.sh
./tests/run_progressive_load_tests.sh
${CMAKE_INSTALL_PREFIX}/bin/tests/test_batch_vs_streaming
```

The [append-job runner](run_append_job_tests.sh) provides direct C++ and gRPC
`append_job()` coverage. Its C++ checks also validate Custom-FCFS
time-accounted resource area, capacity-aware instantaneous and aggregate
utilization (including a non-preemptive overcommit drain), and
prediction-horizon estimation. The cases include multiple same-time
allocations and releases, successive completion-event boundaries, the
post-replay full-capacity tail, the `U=0` fallback, future-job exclusion, and
invalid inputs. The warm-start binary independently checks integrated
effective capacity across capacity changes at and after the start boundary.
[`test_batch_vs_streaming.cpp`](test_batch_vs_streaming.cpp) compares batch and
incremental execution, while
[`run_progressive_load_tests.sh`](run_progressive_load_tests.sh) covers the
separate progressive-file input path. The runnable Python streaming example is
[`python/example_streaming.py`](../python/example_streaming.py).

### Warm-start tests

```bash
ctest --test-dir build --output-on-failure \
  -R '^(test_warm_start|test_warm_start_validation)$'
```

[`test_warm_start.cpp`](test_warm_start.cpp) verifies the replay-based two-stage
path: historical jobs seed occupancy without entering scheduled-job accounting,
ordinary jobs follow the selected runtime policy, and execution switches to
the ordinary event loop after the last warmup job completes. Its cases cover
all supported priority/backfill/queue combinations, randomized differential
workloads, fractional and simultaneous events, empty histories and tails,
capacity overcommit, a capacity change at the final warmup departure, the
zero-start full-replay path, and inclusive `max_time` cutoffs in simulation,
replay, and warm-start modes. The `actual`, `limit`, and deterministic
`distribution` cases confirm that runtime selection remains independent of
warm-start classification.

[`run_warm_start_validation_tests.sh`](run_warm_start_validation_tests.sh)
checks CLI rejection of negative and non-finite start times, invalid maximum
times, non-replay input, and progressive file lists. It requires the simulator
path when invoked directly; CTest supplies that argument automatically.

### Python API tests

After building with `DR_EVT_BUILD_PYTHON=ON`, run:

```bash
./tests/run_python_tests.sh
```

[`test_python_api.py`](test_python_api.py) exercises
configuration, single and batched job append,
time advancement, checkpoint/restart, statistics, warm-start execution,
output, and error handling.

### Checkpoint/restart tests

[`test_checkpoint_restart.cpp`](test_checkpoint_restart.cpp) exercises exact
Ser20 continuation for every standard scheduler backend and callback-based
Custom FCFS. Each scheduler runs a deterministic 256-job workload, checkpoints
with completed, running, waiting, and future jobs present, appends another 256
jobs to both the uninterrupted and restored branches, then compares their
completed schedules and statistics. Additional cases preserve the distinction
between loaded and submitted jobs, resume reclaimed job/resource output in
append mode with byte-for-byte comparison against uninterrupted output, and
reject mismatched configuration. The test is built and registered with CTest
only when Ser20 is available.

### Distributed client/server tests

After building with `DR_EVT_ENABLE_GRPC=ON`, run:

```bash
python3 -m pip install grpcio grpcio-tools protobuf
./tests/run_grpc_tests.sh
python3 tests/test_grpc_single_coordinator.py \
  "${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server"
```

The [gRPC runner](run_grpc_tests.sh) covers
the example client/server pair and, when MPI is available, the multi-client,
multi-server harness. [`run_append_job_tests.sh`](run_append_job_tests.sh) also
tests `AppendJobRequest` over an actual gRPC connection, and
[`test_grpc_single_coordinator.py`](test_grpc_single_coordinator.py) covers the
synchronized independent-systems use case. The related fixtures are in
[`test_traces/grpc/`](test_traces/grpc/).

## Adding tests

Add a fixture to the directory for the behavior it covers and register it in
the corresponding runner. Keep temporary and generated output outside the
repository root.

For a scheduler-correctness fixture:

1. Add `test_traces/scheduler_correctness/<name>.csv`.
2. Add the name to `scripts/generators/generate_all_expected_outputs.py`.
3. Generate and review `<name>.expected_output.csv` and, when applicable,
   `<name>.expected_resources.csv`.
4. Add a small hand-derived `.construction.md` or `.answer.json` when direct
   inspection provides independent confidence.
5. Run `run_scheduler_correctness_tests.sh` and any other affected suite.

Regenerate expected outputs only for an intentional behavior change, and
review the generated diff before committing it.
