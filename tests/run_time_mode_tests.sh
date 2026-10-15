#!/bin/bash
# Run Time Mode Tests
#
# Tests run_time_mode behavioral contracts that other test suites don't cover.
#
# Scheduler uses time_limit as the best estimator for planning (realistic behavior).
# run_time_mode determines how jobs actually execute:
#   * actual: Read actual_run_time from trace (most realistic, default)
#   * limit: Jobs run exactly time_limit (unrealistic, for debugging)
#   * distribution: Sample from statistical distribution
#
# This test verifies:
# 1. run_time_mode=actual reads actual_run_time from trace
# 2. run_time_mode=limit uses time_limit exactly
# 3. run_time_mode=distribution samples and respects time_limit cap
# 4. normal/lognormal distributions are capped at time_limit
# 5. uniform distribution is NOT capped (explicit upper bound by design)

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
echo "Run Time Mode Tests"
echo "=========================================="
echo ""


PASS=0
FAIL=0
TEST_WORK_DIR=$(mktemp -d "/tmp/dr-evt-runtime-modes.XXXXXXXX")
cleanup() { rm -rf -- "$TEST_WORK_DIR"; }
test_report_set_cleanup cleanup

# Use a trace where actual_run_time differs from time_limit
# Job 1: time_limit=200, actual_run_time=50
TRACE="tests/test_traces/scheduler_correctness/25_early_completion_basic.csv"

# ------------------------------------------------------------------------
# Test 1: run_time_mode=actual reads actual_run_time from trace
# ------------------------------------------------------------------------

echo "--- Test 1: run_time_mode=actual reads actual_run_time from trace ---"

"$SIMULATOR" "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode actual \
    --backfill_policy easy \
    --outfile "$TEST_WORK_DIR/actual.csv" \
    > /dev/null 2>&1

# Job 1 is job_submit_time=0, should run for actual_run_time=50s
JOB1_LINE=$(awk -F, '$1 == 0' "$TEST_WORK_DIR/actual.csv")
BEGIN=$(echo "$JOB1_LINE" | cut -d, -f2)
END=$(echo "$JOB1_LINE" | cut -d, -f3)
EXEC_TIME=$((END - BEGIN))

if [ "$EXEC_TIME" -eq 50 ]; then
    echo "✓ PASS: run_time_mode=actual used actual_run_time from trace (50s)"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: expected 50s execution time, got ${EXEC_TIME}s"
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 2: run_time_mode=limit uses time_limit exactly
# ------------------------------------------------------------------------

echo "--- Test 2: run_time_mode=limit uses time_limit exactly ---"

"$SIMULATOR" "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode limit \
    --backfill_policy easy \
    --outfile "$TEST_WORK_DIR/limit.csv" \
    > /dev/null 2>&1

# Job 1 should run for full time_limit=200s
JOB1_LINE=$(awk -F, '$1 == 0' "$TEST_WORK_DIR/limit.csv")
BEGIN=$(echo "$JOB1_LINE" | cut -d, -f2)
END=$(echo "$JOB1_LINE" | cut -d, -f3)
EXEC_TIME=$((END - BEGIN))

if [ "$EXEC_TIME" -eq 200 ]; then
    echo "✓ PASS: run_time_mode=limit used time_limit (200s)"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: expected 200s execution time, got ${EXEC_TIME}s"
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 3: run_time_mode=distribution produces varying results
# ------------------------------------------------------------------------

echo "--- Test 3: run_time_mode=distribution samples from distribution ---"

"$SIMULATOR" "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.5 \
    --run_time_stddev 0.2 \
    --seed 42 \
    --backfill_policy easy \
    --outfile "$TEST_WORK_DIR/dist1.csv" \
    > /dev/null 2>&1

# Different seed should produce different results
"$SIMULATOR" "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.5 \
    --run_time_stddev 0.2 \
    --seed 99 \
    --backfill_policy easy \
    --outfile "$TEST_WORK_DIR/dist2.csv" \
    > /dev/null 2>&1

# Same seed should produce identical results
"$SIMULATOR" "$TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.5 \
    --run_time_stddev 0.2 \
    --seed 42 \
    --backfill_policy easy \
    --outfile "$TEST_WORK_DIR/dist3.csv" \
    > /dev/null 2>&1

# Different seeds should differ
if ! diff -q "$TEST_WORK_DIR/dist1.csv" "$TEST_WORK_DIR/dist2.csv" > /dev/null 2>&1; then
    # Same seed should match
    if diff -q "$TEST_WORK_DIR/dist1.csv" "$TEST_WORK_DIR/dist3.csv" > /dev/null 2>&1; then
        echo "✓ PASS: distribution mode samples vary by seed, deterministic with same seed"
        PASS=$((PASS + 1))
    else
        echo "✗ FAIL: same seed produced different results (not deterministic)"
        FAIL=$((FAIL + 1))
    fi
else
    echo "✗ FAIL: different seeds produced identical results (not sampling)"
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 4: normal/lognormal distributions are capped at time_limit
# ------------------------------------------------------------------------

echo "--- Test 4: normal/lognormal distributions capped at time_limit ---"

LARGE_TRACE="tests/test_traces/scale/huge_10000jobs.csv"
EXPECTED_LARGE_JOBS=$(awk 'END {print NR - 1}' "$LARGE_TRACE")

check_no_exceedance() {
    local dist="$1"
    local outfile="$2"
    local should_cap="$3"  # "yes" or "no"

    "$SIMULATOR" "$LARGE_TRACE" \
        --priority_policy fcfs \
        --total_nodes 500 \
        --trace_format simple \
        --timestamp_format epoch \
        --run_time_mode distribution \
        --run_time_distribution "$dist" \
        --run_time_scale 1.0 \
        --run_time_stddev 0.6 \
        --seed 7 \
        --backfill_policy easy \
        --outfile "$outfile" \
        > /dev/null 2>&1

    "$PYTHON_BIN" - "$outfile" "$dist" "$should_cap" "$EXPECTED_LARGE_JOBS" <<'PY'
import csv
import sys

outfile, distribution, cap_arg, expected_total = sys.argv[1:]
expected_total = int(expected_total)
exceed_count = 0
total = 0
max_excess = 0.0

with open(outfile) as f:
    reader = csv.DictReader(f)
    for row in reader:
        begin = float(row['begin_time'])
        end = float(row['end_time'])
        limit = float(row['time_limit'])
        if begin < -1e18:
            continue  # sentinel: job never ran
        actual_exec = end - begin
        total += 1
        # small tolerance for floating point/integer-second rounding
        if actual_exec > limit + 1.0:
            exceed_count += 1
            max_excess = max(max_excess, actual_exec - limit)

if total != expected_total:
    print(f'✗ FAIL: processed {total}/{expected_total} jobs under {distribution}')
    sys.exit(1)

should_cap = cap_arg == 'yes'

if should_cap:
    # Distribution should be capped
    if exceed_count > 0:
        print(f'✗ FAIL: {exceed_count}/{total} jobs exceeded time_limit under {distribution} (max excess: {max_excess:.1f}s)')
        sys.exit(1)
    else:
        print(f'✓ PASS: 0/{total} jobs exceeded time_limit under {distribution} (properly capped)')
        sys.exit(0)
else:
    # The deterministic fixture and seed must demonstrate the uncapped
    # contract, rather than merely completing without checking it.
    if exceed_count == 0:
        print(f'✗ FAIL: no job exceeded time_limit under uncapped {distribution}')
        sys.exit(1)
    print(f'✓ PASS: {exceed_count}/{total} jobs exceeded time_limit under uncapped {distribution}')
PY
}

if check_no_exceedance "normal" "$TEST_WORK_DIR/normal.csv" "yes"; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if check_no_exceedance "lognormal" "$TEST_WORK_DIR/lognormal.csv" "yes"; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

if check_no_exceedance "uniform" "$TEST_WORK_DIR/uniform.csv" "no"; then
    PASS=$((PASS + 1))
else
    FAIL=$((FAIL + 1))
fi

echo ""

# ------------------------------------------------------------------------
# Test 5: Verify scheduler uses time_limit as best estimator (not actual_run_time)
# ------------------------------------------------------------------------

echo "--- Test 5: Scheduler planning always uses time_limit ---"

# Create a simple trace where using actual_run_time for scheduling would
# produce a different schedule than using time_limit
PLANNING_TRACE="$TEST_WORK_DIR/scheduler-planning.csv"
cat > "$PLANNING_TRACE" <<EOF
job_submit_time,num_nodes,time_limit,actual_run_time
0,60,1000,10
5,50,100,90
EOF

# Job 0: time_limit=1000, actual_run_time=10
# Job 1: time_limit=100, actual_run_time=90
# With 100 nodes total:
# - If scheduler uses time_limit: Job 0 blocks Job 1 (60+50 > 100)
# - If scheduler used actual_run_time: Job 1 could backfill (job 0 would
#   appear to free up at t=10, not t=1000)

"$SIMULATOR" "$PLANNING_TRACE" \
    --priority_policy fcfs \
    --total_nodes 100 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode actual \
    --backfill_policy easy \
    --outfile "$TEST_WORK_DIR/scheduler-actual.csv" \
    > /dev/null 2>&1

# Job 1 should start at t=10 (after job 0 completes), not t=5
# because scheduler plans with time_limit (1000s), not actual_run_time (10s)
JOB1_LINE=$(awk -F, '$1 == 5' "$TEST_WORK_DIR/scheduler-actual.csv")
BEGIN=$(echo "$JOB1_LINE" | cut -d, -f2)

if [ "$BEGIN" -eq 10 ]; then
    echo "✓ PASS: Scheduler uses time_limit for planning (job 1 starts at t=10)"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: Expected job 1 to start at t=10, started at t=${BEGIN}"
    echo "  (Scheduler may be using actual_run_time instead of time_limit)"
    FAIL=$((FAIL + 1))
fi

echo ""

# Test 6: low-tail normal samples are floored at 1 second, not mapped to
# time_limit. Clamping to 0 used to be read back as "unset" at job start
# and replaced by the full limit, so with mean = sd = 0.5 * limit about
# 16% of jobs (the <= 0 tail) ran their whole limit on top of the ~16%
# genuinely capped there.
# ------------------------------------------------------------------------

echo "--- Test 6: low-tail normal samples floored at 1 s, not time_limit ---"

FLOOR_OUT="$TEST_WORK_DIR/normal_floor.csv"
"$SIMULATOR" "$LARGE_TRACE" \
    --priority_policy fcfs \
    --total_nodes 500 \
    --trace_format simple \
    --timestamp_format epoch \
    --run_time_mode distribution \
    --run_time_distribution normal \
    --run_time_scale 0.5 \
    --run_time_stddev 0.5 \
    --seed 7 \
    --backfill_policy easy \
    --outfile "$FLOOR_OUT" \
    > /dev/null 2>&1

if "$PYTHON_BIN" - "$FLOOR_OUT" <<'PY'
import csv, sys
at_limit = at_floor = below_floor = total = 0
with open(sys.argv[1]) as f:
    for row in csv.DictReader(f):
        begin = float(row['begin_time']); end = float(row['end_time'])
        limit = float(row['time_limit'])
        if begin < -1e18:
            continue
        run = end - begin; total += 1
        if run >= limit: at_limit += 1
        if run < 1.0: below_floor += 1
        if abs(run - 1.0) < 1e-9: at_floor += 1
share_limit = at_limit / total; share_floor = at_floor / total
print(f"  {total} jobs: {share_limit:.1%} at time_limit, "
      f"{share_floor:.1%} at the 1 s floor, {below_floor} below it")
# Expected ~16% at the limit (upper tail). The old behaviour doubled it.
ok = below_floor == 0 and share_limit < 0.25 and share_floor > 0.05
sys.exit(0 if ok else 1)
PY
then
    echo "✓ PASS: low-tail samples run for 1 s instead of the full limit"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: low-tail samples still map to time_limit or run below 1 s"
    FAIL=$((FAIL + 1))
fi

echo ""

echo "=========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "=========================================="

if [ "$FAIL" -eq 0 ]; then
    echo "✓ ALL RUN TIME MODE TESTS PASSED"
    exit 0
else
    echo "✗ SOME TESTS FAILED"
    exit 1
fi
