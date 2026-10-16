#!/bin/bash
# gRPC Client/Server Tests
#
# Tests the dr_evt_server/dr_evt_client gRPC streaming interface:
# - A basic, single-pair server/client session (no MPI) - dr_evt_client
#   demonstrates append: it reads job rows client-side and sends each job to
#   the server via AppendJobRequest. The server reads the same file's header
#   during initialization but does not load its job rows (no
#   InitializeTraceRequest is sent - see dr_evt_client.cpp).
# - The MPI-based multi-client/multi-server harness, verifying the
#   lockstep cross-client synchronization produces the expected,
#   hand-computed schedule for two independent, interleaved-arrival-time
#   traces (see tests/test_traces/grpc/trace_a.csv and trace_b.csv)
#
# Requires a build with -DDR_EVT_ENABLE_GRPC=ON (which also implies
# -DDR_EVT_ENABLE_PROTOBUF=ON). The MPI harness additionally requires a
# build where MPI was found (see CMakeLists.txt's find_package(MPI) -
# skipped, not a failure, if MPI isn't available at configure time).

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable
if ! RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/dr-evt-grpc-tests.XXXXXXXX" 2>/dev/null)"; then
    RUN_DIR="$(mktemp -d "/tmp/dr-evt-grpc-tests.XXXXXXXX")"
fi
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf -- "$RUN_DIR"
}
test_report_set_cleanup cleanup

cd "$REPO_ROOT"

echo "=========================================="
echo "gRPC Client/Server Tests"
echo "=========================================="
echo ""

INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX:-$REPO_ROOT/install}"
if [[ "$INSTALL_PREFIX" != /* ]]; then
    INSTALL_PREFIX="$REPO_ROOT/${INSTALL_PREFIX#./}"
fi

if [ ! -x "$INSTALL_PREFIX/bin/dr_evt_server" ] || [ ! -x "$INSTALL_PREFIX/bin/dr_evt_client" ]; then
    echo "Error: dr_evt_server and/or dr_evt_client not found"
    echo "Build with -DDR_EVT_ENABLE_GRPC=ON (implies -DDR_EVT_ENABLE_PROTOBUF=ON)"
    exit 1
fi

SERVER="$INSTALL_PREFIX/bin/dr_evt_server"
CLIENT="$INSTALL_PREFIX/bin/dr_evt_client"

PASS=0
FAIL=0
TRACE_DIR="$REPO_ROOT/tests/test_traces/grpc"

# Ports are chosen at run time so a stale server or an unrelated process on
# the host cannot make the tests fail with "Address already in use". The
# starting points can be overridden with DR_EVT_GRPC_TEST_PORT and
# DR_EVT_GRPC_TEST_BASE_PORT.
port_in_use() {
    (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null
}
# pick_free_ports START COUNT: print the first port of COUNT consecutive
# free ports at or after START, searching 200 ports.
pick_free_ports() {
    local start=$1 count=$2 base port ok
    for ((base = start; base < start + 200; base += count)); do
        ok=1
        for ((port = base; port < base + count; port++)); do
            if port_in_use "$port"; then
                ok=0
                break
            fi
        done
        if (( ok )); then
            echo "$base"
            return 0
        fi
    done
    echo "no free port range found starting at $start" >&2
    return 1
}

# --- Test 1: basic single-pair server/client session ---
echo "Testing: basic_server_client_session"

PORT=$(pick_free_ports "${DR_EVT_GRPC_TEST_PORT:-53001}" 1)
echo "  using port $PORT"
(cd "$RUN_DIR" && exec "$SERVER" "127.0.0.1:${PORT}") \
    > "$RUN_DIR/basic-server.log" 2>&1 &
SERVER_PID=$!
sleep 1

if CLIENT_OUT=$("$CLIENT" "127.0.0.1:${PORT}" "$TRACE_DIR/trace_a.csv" 2>&1); then
    CLIENT_STATUS=0
else
    CLIENT_STATUS=$?
fi
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""

# trace_a.csv (100 nodes, ample capacity, no queuing): job 0 (0->20),
# job 1 (10->25 - overlaps job 0, but both fit within 100 nodes), job 2
# (30->40) - hand-computed makespan is 40.
if [ "$CLIENT_STATUS" -eq 0 ] && \
   echo "$CLIENT_OUT" | grep -q "Jobs completed:  3" && \
   echo "$CLIENT_OUT" | grep -q "Makespan:        40"; then
    echo "  ✓ PASS"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL"
    echo "     Server log:"
    sed 's/^/       /' "$RUN_DIR/basic-server.log"
    echo "     Client output:"
    echo "       Exit status: $CLIENT_STATUS"
    echo "$CLIENT_OUT" | sed 's/^/       /'
    FAIL=$((FAIL + 1))
fi

# --- Test 2: MPI multi-client/multi-server lockstep synchronization ---
echo "Testing: mpi_multi_client_server_lockstep"

MPI_TEST="$INSTALL_PREFIX/bin/tests/test_grpc_multi_client_server"
if [ ! -x "$MPI_TEST" ]; then
    echo "  ⚠ SKIP - $MPI_TEST not found"
    echo "    (MPI not found at configure time, or not yet built)"
else
    if command -v mpirun > /dev/null 2>&1; then
        # GitHub-hosted runners may expose fewer than four Open MPI slots.
        # This test intentionally launches four local ranks, so permit that
        # launcher to oversubscribe.  Other MPI implementations do not all
        # accept this Open MPI-specific option.
        if mpirun --version 2>&1 | grep -qi "Open MPI"; then
            MPI_LAUNCHER=(mpirun --oversubscribe -np 4)
        else
            MPI_LAUNCHER=(mpirun -np 4)
        fi
        MPI_LAUNCHER_NAME="mpirun"
    elif command -v srun > /dev/null 2>&1; then
        MPI_LAUNCHER=(srun --nodes=1 --ntasks=4 --kill-on-bad-exit=1)
        MPI_LAUNCHER_NAME="srun"
    else
        MPI_LAUNCHER=()
        MPI_LAUNCHER_NAME=""
    fi

    if [ -z "$MPI_LAUNCHER_NAME" ]; then
        echo "  ⚠ SKIP - neither mpirun nor srun is available on PATH"
    else
        # The harness binds base_port and base_port + 1 for its two servers.
        MPI_BASE_PORT=$(pick_free_ports "${DR_EVT_GRPC_TEST_BASE_PORT:-53100}" 2)
        echo "  using ports $MPI_BASE_PORT-$((MPI_BASE_PORT + 1))"
        if MPI_OUT=$("${MPI_LAUNCHER[@]}" /bin/bash -c '
            cd "$1"
            shift
            exec "$@"
        ' grpc-test-rank "$RUN_DIR" "$MPI_TEST" \
            "$SERVER" "$MPI_BASE_PORT" \
            "$TRACE_DIR/composite_server1.csv" "$TRACE_DIR/composite_server2.csv" \
            "$TRACE_DIR/composite_jobs.csv" 2>&1); then
            MPI_STATUS=0
        else
            MPI_STATUS=$?
        fi

        # Each server receives four ordinary jobs and two composite fragments.
        # The output also proves the equal (t1 == t2 == t3) and strict
        # (t1 < t2 < t3 < t4) AppendJobs/AdvanceTo boundaries were reached.
        if [ "$MPI_STATUS" -eq 0 ] && \
           echo "$MPI_OUT" | grep -q "submitted=6 completed=6" && \
           [ "$(echo "$MPI_OUT" | grep -c "submitted=6 completed=6")" -eq 2 ] && \
           echo "$MPI_OUT" | grep -q "t1 == t2" && \
           echo "$MPI_OUT" | grep -q "t1 < t2" && \
           echo "$MPI_OUT" | grep -q "composite at t3 == t2" && \
           echo "$MPI_OUT" | grep -q "composite at t3 after ordinary batch"; then
            echo "  ✓ PASS"
            PASS=$((PASS + 1))
        else
            echo "  ✗ FAIL"
            echo "     $MPI_LAUNCHER_NAME output:"
            echo "$MPI_OUT" | sed 's/^/       /'
            FAIL=$((FAIL + 1))
        fi
    fi
fi

echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL GRPC TESTS PASSED"
    exit 0
else
    echo "✗ SOME GRPC TESTS FAILED"
    exit 1
fi
