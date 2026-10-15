#!/bin/bash
# Test that configuration files produce same results as command-line options
#
# Verifies that all CLI options are supported via config files and behave identically
# NOTE: Requires Protobuf support (cmake -DDR_EVT_ENABLE_PROTOBUF=ON)
#
# This test uses --run_time_mode limit throughout (test traces lack actual_run_time column).
# Both CLI and config-file invocations use the same run_time_mode for meaningful comparison.

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$SCRIPT_DIR/.."
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable

cd "$REPO_ROOT"

# Source common simulator path finder
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/select_python.sh"
if ! select_python_interpreter 3 6; then
    echo "Error: Python 3.6 or newer is required" >&2
    exit 1
fi

echo "=========================================="
echo "Configuration File Tests"
echo "=========================================="
echo ""
echo "Testing that config files match CLI options"
echo ""


# Check if simulator supports --config option (requires Protobuf)
if ! $SIMULATOR --help 2>&1 | grep -q -- "--config"; then
    echo "Simulator built without Protobuf support (no --config option)"
    echo "Skipping config tests (require -DDR_EVT_ENABLE_PROTOBUF=ON)"
    test_report_skip "simulator was built without Protobuf support"
    exit 0
fi

# Test trace
TEST_TRACE="tests/test_traces/unit/simple_basic.csv"

if [ ! -f "$TEST_TRACE" ]; then
    echo "Error: Test trace not found: $TEST_TRACE"
    exit 1
fi

PASS=0
FAIL=0
TEST_WORK_DIR=$(mktemp -d "/tmp/dr-evt-config.XXXXXXXX")
cleanup() { rm -rf -- "$TEST_WORK_DIR"; }
test_report_set_cleanup cleanup

# Test 1: Minimal config vs CLI
echo "Test 1: Minimal config"
$SIMULATOR "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --outfile "$TEST_WORK_DIR/cli_minimal.csv"

$SIMULATOR "$TEST_TRACE" \
    --config tests/test_configs/minimal_config.pb \
    --outfile "$TEST_WORK_DIR/pb_minimal.csv"

if diff -q "$TEST_WORK_DIR/cli_minimal.csv" "$TEST_WORK_DIR/pb_minimal.csv" > /dev/null; then
    echo "  ✓ Minimal config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ Minimal config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 2: Full config vs CLI
echo "Test 2: Full config"
$SIMULATOR "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --backfill_policy easy \
    --priority_policy fcfs \
    --num_max_candidates 8 \
    --outfile "$TEST_WORK_DIR/cli_full.csv"

$SIMULATOR "$TEST_TRACE" \
    --config tests/test_configs/full_config.pb \
    --outfile "$TEST_WORK_DIR/pb_full.csv"

if diff -q "$TEST_WORK_DIR/cli_full.csv" "$TEST_WORK_DIR/pb_full.csv" > /dev/null; then
    echo "  ✓ Full config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ Full config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 3: Conservative config vs CLI
echo "Test 3: Conservative policy config"
CONSERVATIVE_TRACE="tests/test_traces/feature/easy_vs_conservative_test.csv"
$SIMULATOR "$CONSERVATIVE_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --backfill_policy conservative \
    --outfile "$TEST_WORK_DIR/cli_conservative.csv"

$SIMULATOR "$CONSERVATIVE_TRACE" \
    --config tests/test_configs/conservative_config.pb \
    --outfile "$TEST_WORK_DIR/pb_conservative.csv"

if diff -q "$TEST_WORK_DIR/cli_conservative.csv" "$TEST_WORK_DIR/pb_conservative.csv" > /dev/null; then
    echo "  ✓ Conservative config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ Conservative config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 4: Distribution config
echo "Test 4: Distribution config"
$SIMULATOR "$TEST_TRACE" \
    --config tests/test_configs/distribution_config.pb \
    --outfile "$TEST_WORK_DIR/pb_distribution.csv"

$SIMULATOR "$TEST_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.9 \
    --run_time_stddev 0.1 \
    --outfile "$TEST_WORK_DIR/cli_distribution.csv"

if diff -q "$TEST_WORK_DIR/cli_distribution.csv" "$TEST_WORK_DIR/pb_distribution.csv" > /dev/null; then
    echo "  ✓ Distribution config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ Distribution config failed"
    FAIL=$((FAIL + 1))
fi

# Test 5: infile_list via protobuf config (progressive loading) vs the
# same via CLI --infile_list - no positional trace file here, unlike
# the tests above: infile_list is mutually exclusive with one.
echo "Test 5: infile_list config (progressive loading)"
$SIMULATOR \
    --infile_list tests/test_traces/progressive/file_list.txt \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --outfile "$TEST_WORK_DIR/cli_infile_list.csv"

$SIMULATOR \
    --config tests/test_configs/infile_list_config.pb \
    --outfile "$TEST_WORK_DIR/pb_infile_list.csv"

if diff -q "$TEST_WORK_DIR/cli_infile_list.csv" "$TEST_WORK_DIR/pb_infile_list.csv" > /dev/null; then
    echo "  ✓ infile_list config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ infile_list config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 6: check_memory_pressure via protobuf config - forced
# deterministically via the DR_EVT_TEST_AVAILABLE_MEMORY_BYTES test
# seam (see get_available_memory_bytes()'s doc comment in
# src/utils/system_memory.hpp), not by relying on the test machine
# actually being low on memory.
echo "Test 6: check_memory_pressure config (forced low memory)"

if DR_EVT_TEST_AVAILABLE_MEMORY_BYTES=10 $SIMULATOR \
    --config tests/test_configs/memory_pressure_config.pb \
    --outfile "$TEST_WORK_DIR/pb_memory_pressure.csv" > "$TEST_WORK_DIR/pb_memory_pressure.log" 2>&1; then
    echo "  ✗ FAIL - expected nonzero exit under forced low memory with check_memory_pressure enabled"
    FAIL=$((FAIL + 1))
else
    if grep -q "check_memory_pressure" "$TEST_WORK_DIR/pb_memory_pressure.log"; then
        echo "  ✓ check_memory_pressure config correctly refused under forced low memory"
        PASS=$((PASS + 1))
    else
        echo "  ✗ FAIL - wrong error message"
        sed 's/^/     /' "$TEST_WORK_DIR/pb_memory_pressure.log"
        FAIL=$((FAIL + 1))
    fi
fi

# Test 7: every protobuf-config example in the actual documentation
# (docs/user-guide/protobuf-config.md) must itself parse and run -
# not just the hand-verified fixtures under tests/test_configs/ above,
# which can never catch a bug in the documentation's own prose
# examples (this test exists because exactly that happened: every
# example wrapped its fields in a fictional "sim_setup { ... }" block,
# and several "Run with" instructions omitted the required positional
# trace-file argument - see tests/test_protobuf_config_doc_examples.py's
# own docstring for the full story).
echo "Test 7: protobuf-config.md's own documented examples"
if SIMULATOR="$SIMULATOR" "$PYTHON_BIN" tests/test_protobuf_config_doc_examples.py \
    > "$TEST_WORK_DIR/doc_examples.log" 2>&1; then
    echo "  ✓ all documented config examples parse and run correctly"
    PASS=$((PASS + 1))
else
    echo "  ✗ FAIL - one or more documented config examples are broken"
    sed 's/^/     /' "$TEST_WORK_DIR/doc_examples.log"
    FAIL=$((FAIL + 1))
fi

# Test 8: trace_type=pcon via protobuf config must behave identically to the
# equivalent CLI selection, including the power-usage resource columns.
echo "Test 8: power-usage trace_type config"
PCON_TRACE="tests/test_traces/pcon_cli_input.csv"

$SIMULATOR "$PCON_TRACE" \
    --trace_type pcon \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --total_nodes 4 \
    --outfile "$TEST_WORK_DIR/cli_pcon.csv" \
    --resource_trace "$TEST_WORK_DIR/cli_pcon_resources.csv"

$SIMULATOR "$PCON_TRACE" \
    --config tests/test_configs/pcon_config.pb \
    --outfile "$TEST_WORK_DIR/pb_pcon.csv" \
    --resource_trace "$TEST_WORK_DIR/pb_pcon_resources.csv"

if diff -q "$TEST_WORK_DIR/cli_pcon.csv" "$TEST_WORK_DIR/pb_pcon.csv" > /dev/null && \
   diff -q "$TEST_WORK_DIR/cli_pcon_resources.csv" "$TEST_WORK_DIR/pb_pcon_resources.csv" > /dev/null; then
    echo "  ✓ Power-usage trace_type config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ Power-usage trace_type config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 9: invalid trace_type supplied through protobuf config must fail with
# the same kind of explicit diagnostic as an invalid CLI value.
echo "Test 9: Invalid trace_type config"
INVALID_TRACE_TYPE_CONFIG="$TEST_WORK_DIR/invalid_trace_type.pb"
cat > "$INVALID_TRACE_TYPE_CONFIG" <<'EOF'
trace_type: "invalid"
EOF

if $SIMULATOR "$TEST_TRACE" \
    --config "$INVALID_TRACE_TYPE_CONFIG" \
    --outfile "$TEST_WORK_DIR/pb_invalid_trace_type.csv" \
    > "$TEST_WORK_DIR/pb_invalid_trace_type.log" 2>&1; then
    echo "  ✗ Invalid trace_type config unexpectedly succeeded"
    FAIL=$((FAIL + 1))
else
    if grep -q "Unknown trace_type in protobuf" "$TEST_WORK_DIR/pb_invalid_trace_type.log"; then
        echo "  ✓ Invalid trace_type config correctly rejected"
        PASS=$((PASS + 1))
    else
        echo "  ✗ Invalid trace_type failed with unexpected error"
        sed 's/^/     /' "$TEST_WORK_DIR/pb_invalid_trace_type.log"
        FAIL=$((FAIL + 1))
    fi
fi

# Test 10: capacity_schedule is available through protobuf field 30, and a
# later CLI option overrides the config value just like the other options.
echo "Test 10: capacity schedule config and CLI precedence"
CAPACITY_TRACE="tests/test_traces/feature/capacity_schedule.csv"
CAPACITY_CONFIG="$TEST_WORK_DIR/capacity_config.pb"
CAPACITY_OVERRIDE="$TEST_WORK_DIR/capacity_override.csv"
cat > "$CAPACITY_CONFIG" <<'EOF'
total_nodes: 100
trace_format: "simple"
timestamp_format: "epoch"
run_time_mode: "limit"
capacity_schedule: "tests/test_traces/feature/capacity_schedule.capacity.csv"
EOF
cat > "$CAPACITY_OVERRIDE" <<'EOF'
time,total_nodes
1000,25
EOF

$SIMULATOR "$CAPACITY_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --capacity_schedule tests/test_traces/feature/capacity_schedule.capacity.csv \
    --outfile "$TEST_WORK_DIR/cli_capacity.csv"

$SIMULATOR "$CAPACITY_TRACE" \
    --config "$CAPACITY_CONFIG" \
    --outfile "$TEST_WORK_DIR/pb_capacity.csv"

$SIMULATOR "$CAPACITY_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --capacity_schedule "$CAPACITY_OVERRIDE" \
    --outfile "$TEST_WORK_DIR/cli_capacity_override.csv"

$SIMULATOR "$CAPACITY_TRACE" \
    --config "$CAPACITY_CONFIG" \
    --capacity_schedule "$CAPACITY_OVERRIDE" \
    --outfile "$TEST_WORK_DIR/pb_capacity_override.csv"

if diff -q "$TEST_WORK_DIR/cli_capacity.csv" "$TEST_WORK_DIR/pb_capacity.csv" > /dev/null && \
   diff -q "$TEST_WORK_DIR/cli_capacity_override.csv" "$TEST_WORK_DIR/pb_capacity_override.csv" > /dev/null; then
    echo "  ✓ capacity schedule config and later CLI override both match"
    PASS=$((PASS + 1))
else
    echo "  ✗ capacity schedule config or CLI precedence differs"
    FAIL=$((FAIL + 1))
fi

# Test 11: sim_start_time is available through protobuf field 31 and produces the
# same replay-based warm-start schedule as the CLI.
echo "Test 11: warm-start time config"
WARM_TRACE="tests/test_traces/feature/warm_start_native.csv"
WARM_CONFIG="$TEST_WORK_DIR/warm_start_config.pb"
cat > "$WARM_CONFIG" <<'EOF'
total_nodes: 100
trace_format: "simple"
timestamp_format: "epoch"
run_time_mode: "actual"
sim_start_time: 50
EOF

$SIMULATOR "$WARM_TRACE" \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode actual \
    --sim_start_time 50 \
    --outfile "$TEST_WORK_DIR/cli_warm_start.csv"

$SIMULATOR "$WARM_TRACE" \
    --config "$WARM_CONFIG" \
    --outfile "$TEST_WORK_DIR/pb_warm_start.csv"

if diff -q "$TEST_WORK_DIR/cli_warm_start.csv" \
           "$TEST_WORK_DIR/pb_warm_start.csv" > /dev/null; then
    echo "  ✓ sim_start_time config matches CLI"
    PASS=$((PASS + 1))
else
    echo "  ✗ sim_start_time config differs from CLI"
    FAIL=$((FAIL + 1))
fi

# Test 12: protobuf sim_start_time validation rejects every invalid numeric class.
echo "Test 12: invalid simulation-start-time config"
INVALID_START_OK=1
for VALUE in -1 nan inf; do
    INVALID_CONFIG="$TEST_WORK_DIR/invalid_start_${VALUE}.pb"
    INVALID_LOG="$TEST_WORK_DIR/invalid_start_${VALUE}.log"
    printf 'sim_start_time: %s\n' "$VALUE" > "$INVALID_CONFIG"
    if $SIMULATOR "$WARM_TRACE" --config "$INVALID_CONFIG" \
        > "$INVALID_LOG" 2>&1; then
        INVALID_START_OK=0
    elif ! grep -q "sim_start_time" "$INVALID_LOG"; then
        INVALID_START_OK=0
    fi
done
if [ "$INVALID_START_OK" -eq 1 ]; then
    echo "  ✓ negative and non-finite sim_start_time values are rejected"
    PASS=$((PASS + 1))
else
    echo "  ✗ an invalid sim_start_time was accepted or misdiagnosed"
    FAIL=$((FAIL + 1))
fi

# Test 13: a --config file that cannot be read must stop the run. Previously
# a missing file or a parse error was only printed, and the simulation went
# on with default parameters and exit status 0.
echo "Test 13: unreadable config file is rejected"
UNREADABLE_OK=1
BAD_CONFIG="$TEST_WORK_DIR/bad_field.pb"
printf "total_nodez: 5\n" > "$BAD_CONFIG"
for CFG in "$TEST_WORK_DIR/does_not_exist.pb" "$BAD_CONFIG"; do
    CFG_LOG="$TEST_WORK_DIR/unreadable_$(basename "$CFG").log"
    if $SIMULATOR "$TEST_TRACE" --config "$CFG" > "$CFG_LOG" 2>&1; then
        UNREADABLE_OK=0
    elif ! grep -q "failed to read config file" "$CFG_LOG"; then
        UNREADABLE_OK=0
    fi
done
if [ "$UNREADABLE_OK" -eq 1 ]; then
    echo "  ✓ missing and unparsable config files are rejected with nonzero exit"
    PASS=$((PASS + 1))
else
    echo "  ✗ an unreadable config file was accepted or misdiagnosed"
    FAIL=$((FAIL + 1))
fi
echo ""
echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ $FAIL -eq 0 ]; then
    echo "✓ ALL CONFIG TESTS PASSED"
    exit 0
else
    echo "✗ SOME CONFIG TESTS FAILED"
    exit 1
fi
