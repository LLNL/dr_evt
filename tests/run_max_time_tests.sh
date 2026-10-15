#!/bin/bash
# End-to-end checks for the simulator's inclusive --max_time boundary.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable

if [ "$#" -gt 2 ]; then
    echo "Usage: $0 [simulator [tracer]]" >&2
    exit 2
fi

if [ "$#" -ge 1 ]; then
    SIMULATOR="$1"
fi
if [ "$#" -eq 2 ]; then
    TRACER="$2"
fi
source "$SCRIPT_DIR/set_simulator_path.sh"
source "$SCRIPT_DIR/set_tracer_path.sh"
if ! WORK_DIR="$(mktemp -d \
    "${TMPDIR:-/tmp}/dr-evt-max-time.XXXXXXXX" 2>/dev/null)"; then
    WORK_DIR="$(mktemp -d "/tmp/dr-evt-max-time.XXXXXXXX")"
fi
cleanup() { rm -rf -- "$WORK_DIR"; }
test_report_set_cleanup cleanup

fail() {
    echo "max_time test failed: $1" >&2
    exit 1
}

expect_line() {
    grep -Fqx -- "$1" "$2" || fail "missing '$1' in $2"
}

# Ordinary simulation: the t=5 completion and arrival are both processed.
# The second job remains running, and the t=20 arrival remains pending.
cat >"$WORK_DIR/simulation.csv" <<'EOF'
job_submit_time,num_nodes,time_limit,actual_run_time
0,2,5,5
5,2,10,10
20,1,1,1
EOF
"$SIMULATOR" "$WORK_DIR/simulation.csv" --total_nodes 2 \
    --run_time_mode actual --max_time 5 \
    --outfile "$WORK_DIR/simulation.out.csv" \
    --resource_trace "$WORK_DIR/simulation.resources.csv" \
    >"$WORK_DIR/simulation.log" 2>&1 || fail "ordinary simulation exited nonzero"
expect_line "Current time: 5" "$WORK_DIR/simulation.log"
expect_line "Jobs completed: 1" "$WORK_DIR/simulation.log"
[ "$(wc -l <"$WORK_DIR/simulation.out.csv")" -eq 2 ] || \
    fail "ordinary output should contain exactly one completed job"
expect_line "0,0,5,2,0,5" "$WORK_DIR/simulation.out.csv"
[ "$(tail -n 1 "$WORK_DIR/simulation.resources.csv")" = "5,0,2" ] || \
    fail "ordinary simulation did not settle the inclusive t=5 events"

# Full replay uses recorded begin/end times but observes the same boundary.
cat >"$WORK_DIR/replay.csv" <<'EOF'
job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit
0,0,5,2,0,5
4,5,10,2,0,5
11,11,12,1,0,1
EOF
"$SIMULATOR" "$WORK_DIR/replay.csv" --total_nodes 4 --max_time 5 \
    --outfile "$WORK_DIR/replay.out.csv" \
    --resource_trace "$WORK_DIR/replay.resources.csv" \
    >"$WORK_DIR/replay.log" 2>&1 || fail "replay exited nonzero"
expect_line "Current time: 5" "$WORK_DIR/replay.log"
expect_line "Jobs completed: 1" "$WORK_DIR/replay.log"
[ "$(wc -l <"$WORK_DIR/replay.out.csv")" -eq 2 ] || \
    fail "replay output should contain exactly one completed job"
[ "$(tail -n 1 "$WORK_DIR/replay.resources.csv")" = "5,2,2" ] || \
    fail "replay did not settle the inclusive t=5 events"

# A limit reached during warmup must stop there without entering an unbounded
# ordinary drain. Both the historical and newly scheduled job are still live.
cat >"$WORK_DIR/warm.csv" <<'EOF'
job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit
0,2,15,2,0,13
10,20,24,2,0,4
EOF
"$SIMULATOR" "$WORK_DIR/warm.csv" --total_nodes 4 \
    --sim_start_time 10 --max_time 12 --run_time_mode actual \
    --outfile "$WORK_DIR/warm.out.csv" \
    --resource_trace "$WORK_DIR/warm.resources.csv" \
    >"$WORK_DIR/warm.log" 2>&1 || fail "warm start exited nonzero"
expect_line "Current time: 12" "$WORK_DIR/warm.log"
expect_line "Jobs completed: 0" "$WORK_DIR/warm.log"
[ "$(wc -l <"$WORK_DIR/warm.out.csv")" -eq 1 ] || \
    fail "warm-start output should contain no completed jobs"
[ "$(tail -n 1 "$WORK_DIR/warm.resources.csv")" = "10,0,4" ] || \
    fail "warm-start live occupancy is incorrect at the boundary"

# Tracer: -t/--max_time is the same inclusive event-time boundary as the
# simulator's. It used to be parsed and then ignored. The replay fixture has
# events at 0, 20, 70, 200, 200, and 500; the resource trace writes a
# baseline row plus one row per processed event.
REPLAY="$REPO_ROOT/tests/test_traces/replay/optional_reports.expected_jobs.csv"
"$TRACER" "$REPLAY" --total_nodes 100 \
    --resource_trace "$WORK_DIR/tracer.all.csv" \
    >"$WORK_DIR/tracer.all.log" 2>&1 || fail "tracer without -t exited nonzero"
"$TRACER" "$REPLAY" --total_nodes 100 --max_time 199 \
    --resource_trace "$WORK_DIR/tracer.199.csv" \
    >"$WORK_DIR/tracer.199.log" 2>&1 || fail "tracer -t 199 exited nonzero"
"$TRACER" "$REPLAY" --total_nodes 100 --max_time 200 \
    --resource_trace "$WORK_DIR/tracer.200.csv" \
    >"$WORK_DIR/tracer.200.log" 2>&1 || fail "tracer -t 200 exited nonzero"
[ "$(wc -l <"$WORK_DIR/tracer.all.csv")" -eq 8 ] || \
    fail "tracer without -t should write 8 resource rows"
[ "$(wc -l <"$WORK_DIR/tracer.199.csv")" -eq 5 ] || \
    fail "tracer -t 199 should stop before the events at t=200"
[ "$(tail -n 1 "$WORK_DIR/tracer.199.csv" | cut -d, -f1)" = "70" ] || \
    fail "tracer -t 199 last event should be at t=70"
[ "$(wc -l <"$WORK_DIR/tracer.200.csv")" -eq 7 ] || \
    fail "tracer -t 200 should include both events at t=200 (inclusive)"
[ "$(tail -n 1 "$WORK_DIR/tracer.200.csv" | cut -d, -f1)" = "200" ] || \
    fail "tracer -t 200 last event should be at t=200"
if "$TRACER" "$REPLAY" --total_nodes 100 --max_time -1 \
        >"$WORK_DIR/tracer.neg.log" 2>&1; then
    fail "tracer -t -1 should be rejected"
fi
grep -q "max_time must be finite and nonnegative" \
    "$WORK_DIR/tracer.neg.log" || \
    fail "tracer -t -1 rejection lacks the expected message"
if "$TRACER" "$REPLAY" --total_nodes 100 --max_time abc \
        >"$WORK_DIR/tracer.abc.log" 2>&1; then
    fail "tracer -t abc should be rejected"
fi
grep -q "max_time expects a number" "$WORK_DIR/tracer.abc.log" || \
    fail "tracer -t abc rejection lacks the expected message"
if "$SIMULATOR" "$WORK_DIR/simulation.csv" --total_nodes 2 --max_time abc \
        >"$WORK_DIR/sim.abc.log" 2>&1; then
    fail "simulator --max_time abc should be rejected"
fi
grep -q "max_time expects a number" "$WORK_DIR/sim.abc.log" || \
    fail "simulator --max_time abc rejection lacks the expected message"

echo "max_time CLI tests passed"
