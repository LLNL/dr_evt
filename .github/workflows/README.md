# GitHub Actions CI/CD Workflows

This directory contains GitHub Actions workflows for automated testing.

## Workflows

### 1. `tests.yml` - Full Test Suite

**Triggers:**
- Push to `main` or `develop` branches
- Pull requests to `main` or `develop`
- Manual trigger via GitHub UI

**What it runs:**
- Scheduler correctness tests (34) - `tests/run_scheduler_correctness_tests.sh`
- Queue implementation differential tests
- Column alias tests (8)
- Run-time mode tests (8)
- Unit tests (7)
- Feature tests (8)
- Conservative backfilling tests (2)
- Replay tests (5: one reclamation-boundary binary and four CLI comparisons)
- Resource-history tests (5)
- Job-store tests (6)
- Config tests (14), including power-usage `trace_type`, capacity-schedule,
  and warm-start coverage
- Native CTest suite (19 native registrations, the Python trace-tools and
  multi-cluster dispatch registrations, and the MPI streaming and MPI dispatch
  registrations when MPI is available), including the
  custom FCFS scheduler's focused and 2,000-job comparisons
- Ser20-disabled native serialization build and tests
- Sphinx and Doxygen documentation build with warnings treated as errors
- Python API tests (18)
- Market package platform and job-stream tests (12)
- gRPC client/server tests (2)
- Append-job tests (23 C++ + 5 optional gRPC checks)
- FCFS/EASY backfill-window focused rerun of the five-check gRPC binary
- Synchronized single-coordinator gRPC test
- Progressive-loading tests (C++ + CLI)
- Queue-input schema test
- Redis-enabled build, isolated Redis server integration test, and real gRPC
  client Redis-first/status-fallback test
- Scale tests (7)

**Matrix:**
- GCC 14
- Clang 18
- Python 3.12

**Duration:** ~5-10 minutes

### 2. `quick-test.yml` - Quick Scheduler and Trace-Schema Check

**Triggers:**
- Push to any branch (except `main`)
- Manual trigger

**What it runs:**
- Scheduler correctness fixtures (34) - `tests/run_scheduler_correctness_tests.sh`
- Ser20 caller-owned-memory serialization test - installed `t_state_ser20`
- Progressive-loading C++ API test - installed `test_progressive_load`
- Queue-input schema test - installed `test_queue_input`
- Streaming append API test - installed `test_append_job_api`

**Compiler:**
- GCC 14 only

**Duration:** ~2-3 minutes

**Purpose:** Fast validation for development branches

## Test Coverage

Total tests referenced by the full suite:

| Category | Count | Verified in this doc pass? |
|----------|-------|------------------------------|
| Scheduler correctness | 34 | CI runner |
| Custom FCFS | 8 | CTest; six focused checks and two golden schedules, including warm-start accounting and 2,000 jobs |
| Queue implementation differential | 34 fixtures × 4 implementations | CI runner |
| Column aliases | 8 | CI runner |
| Run-time mode | 8 | CI runner |
| Unit | 7 | CI runner |
| Feature | 8 | CI runner; includes time-varying capacity and native warm start |
| Conservative | 2 | CI runner |
| Replay | 5 | CI runner; reclamation safety plus resource equivalence |
| Resource history | 5 | CI runner |
| Job store | 6 | CI runner |
| Config | 14 | CI runner; includes power-usage, capacity-schedule, and warm-start configuration coverage |
| Native CTest | 19 native, 2 Python, plus 2 with MPI | CI runner; RNG and binary serialization, trace policies, replay reclamation, custom scheduling, append/streaming APIs, warm starts, capacity parsing, queues, CLI dispatch, and SWF input |
| Trace tools | 4 checks in 1 CTest registration | CI runner; capacity inference, simulator-format conversion, checkpoint output stitching, and warm-start boundary/output behavior |
| Python API | 20 | CI runner |
| Market package | 12 | CI runner; platforms, jobs and trace preparation |
| gRPC client/server | 2 | CI runner |
| Append-job | 28: 23 C++ + 5 optional gRPC checks | CI runner |
| FCFS/EASY backfill-window gRPC | 5 repeated checks; 1 targeted | CI runner |
| Single-coordinator gRPC | 1 | CI runner; synchronized independent systems |
| Progressive loading | 11 C++ + 5 CLI | CI runner |
| Queue input schema | 1 binary | CI runner; queue variants and replay/simulation runtime validation |
| Redis output | 1 integration runner | Dedicated Redis-enabled GCC build; server startup, typed batched job/resource serialization, structured resource and Pcon hashes, indexes, namespace replacement, finalized-versus-unfinished visibility, pipelined bulk lookup, and byte-identical job and resource outputs for Redis/file runs of 200 jobs |
| Redis gRPC example | 1 integration runner | Real client/server batch append, advance, pipelined Redis lookup, missing-ID server fallback, and ordered merge |
| Scale | 7 | CI runner |

The workflow summary in `tests.yml` is the authoritative CI-oriented list.
See `docs/TESTING_GUIDE.md` for the fuller test catalog and the distinction
between individual assertions, fixtures, binaries, and runner-level counts.
The native CTest row overlaps with individually listed tests that CTest invokes.

## Status Badges

Add to main README.md:

```markdown
[![Tests](https://github.com/LLNL/dr_evt/workflows/DR_EVT%20Test%20Suite/badge.svg)](https://github.com/LLNL/dr_evt/actions)
```

## Local Testing

Run the same tests locally before pushing:

```bash
export CMAKE_INSTALL_PREFIX="${PWD}/install"
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}" \
  -DDR_EVT_BUILD_PYTHON=ON \
  -DDR_EVT_ENABLE_GRPC=ON \
  -DDR_EVT_WITH_SER20=ON \
  -DDR_EVT_WITH_UNIT_TESTING=ON
cmake --build build -j4
cmake --install build

python3 -m pip install grpcio grpcio-tools protobuf

./tests/run_scheduler_correctness_tests.sh
./tests/run_custom_scheduler_tests.sh
./tests/run_fcfs_queue_implementation_tests.sh --correctness
./tests/run_column_alias_tests.sh
./tests/run_time_mode_tests.sh
./tests/run_unit_tests.sh
./tests/run_feature_tests.sh
./tests/run_easy_vs_conservative_correctness_tests.sh
./tests/run_replay_tests.sh
./tests/run_resource_history_tests.sh
./tests/run_job_store_tests.sh
./tests/run_configs_tests.sh
./tests/run_python_tests.sh
./tests/run_grpc_tests.sh
./tests/run_append_job_tests.sh
./tests/run_backfill_window_grpc_test.sh
python3 tests/test_grpc_single_coordinator.py \
  "${CMAKE_INSTALL_PREFIX}/bin/dr_evt_server"
./tests/run_progressive_load_tests.sh
./tests/run_scale_tests.sh
ctest --test-dir build --output-on-failure

# Verify the native binary-state path remains usable without Ser20.
cmake -S . -B build-no-ser20 \
  -DDR_EVT_WITH_SER20=OFF \
  -DDR_EVT_ENABLE_PROTOBUF=OFF \
  -DDR_EVT_ENABLE_GRPC=OFF
cmake --build build-no-ser20 \
  --target t_state_rngen-bin t_state-bin -j4
./build-no-ser20/t_state_rngen
./build-no-ser20/t_state 10 42 4 2

# Validate the documentation with the same strictness as CI.
python3 -m pip install -r docs/requirements.txt
make -C docs html SPHINXOPTS="-W --keep-going"
```

There is no `run_correctness_tests.sh` in this checkout - an earlier
version of this document referenced it, but the actual scheduler_correctness/
runner is `run_scheduler_correctness_tests.sh`.

## Workflow Details

### Build Steps

1. Install dependencies (CMake, Boost, Protobuf/gRPC, MPI, Python, compilers)
2. Configure a C++20 Release build with Ser20, Python bindings, Protobuf, and
   gRPC enabled. CI exercises Ser20's FetchContent fallback because no system
   Ser20 package is installed.
3. Build with all CPU cores (`make -j$(nproc)`). You may cap it to -j2
   as defense-in-depth against the gRPC/BoringSSL FetchContent OOM
   issue; see `docs/getting-started/installation.md`)
4. Verify build artifacts exist
5. Build without Ser20 and run the native serialization tests
6. Build the Sphinx and Doxygen documentation with warnings as errors

### Test Steps

Each test category runs independently - see "Test Coverage" above for
what each actually covers and what's been verified.

### Artifacts

On test failure, uploads:
- Test output CSVs from `/tmp/`
- CMake test logs
- Retained for 7 days

## Adding New Tests

When you add a new test:

1. Add to the appropriate test category and directory
2. For `scheduler_correctness/`, add the test name to `run_scheduler_correctness_tests.sh`'s
   `TESTS` array and to `scripts/generators/generate_all_expected_outputs.py`'s
   `TESTS` list (to generate its expected output); for `scale/`, use
   `scripts/generators/generate_scale_expected_outputs.py`
3. CI will automatically pick it up via the existing runner scripts - no
   workflow file changes needed unless you're adding a wholly new
   category

## Troubleshooting

### Workflow fails but tests pass locally

- Check compiler version (the full CI matrix uses GCC 14 and Clang 18; the
  quick workflow uses GCC 14)
- Check Boost version
- Run with same flags as CI: `-DCMAKE_BUILD_TYPE=Release`

### Build fails in CI

- Check dependencies in `tests.yml`
- Check CMakeLists.txt for platform-specific issues

### `28_simultaneous_completions_backfill` fails in `run_scheduler_correctness_tests.sh`

If you're running an older copy of `run_scheduler_correctness_tests.sh`: this was a real,
known false-failure in the script's resource-trace comparison (too strict
about the internal order of simultaneous end/start events within the
same timestamp, not an actual scheduling bug) - fixed by consolidating to
the settled state per timestamp before comparing. If you still see this
on a current copy of the script, it may indicate a genuine regression -
don't assume it's the same already-fixed issue without checking.

### Tests timeout

- The `redis-integration` job sets `timeout-minutes: 15`; the other jobs use
  GitHub Actions' six-hour default.
- Add a job-level `timeout-minutes` value to the other jobs if a tighter limit
  is needed.
- Consider splitting into more jobs

## Future Enhancements

Potential additions:

- [ ] Code coverage reporting (lcov/gcov)
- [ ] Performance benchmarking
- [ ] Nightly builds with extended tests
- [ ] Docker-based builds for reproducibility
- [ ] Multi-platform testing (macOS, Windows)
- [ ] Memory leak detection (valgrind)
- [ ] Static analysis (clang-tidy, cppcheck)

## References

- **GitHub Actions docs:** https://docs.github.com/en/actions
- **Test documentation:** `../tests/README.md`
- **Testing methodology and known limitations:** `../docs/TESTING_GUIDE.md`
