#!/bin/bash
# Checks for how the simulator accepts its input on the command line: the
# trace comes from either -i/--infile or one positional argument, never both
# and at least one; and unsigned numeric values in options and trace rows
# must be rejected when negative, non-numeric, or out of range.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/test_reporting.sh"
test_report_enable

if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [simulator]" >&2
    exit 2
fi

if [ "$#" -eq 1 ]; then
    SIMULATOR="$1"
fi
source "$SCRIPT_DIR/set_simulator_path.sh"
if ! WORK_DIR="$(mktemp -d \
    "${TMPDIR:-/tmp}/dr-evt-cli-input.XXXXXXXX" 2>/dev/null)"; then
    WORK_DIR="$(mktemp -d "/tmp/dr-evt-cli-input.XXXXXXXX")"
fi
cleanup() { rm -rf -- "$WORK_DIR"; }
test_report_set_cleanup cleanup

PASS=0
FAIL=0

TRACE="$WORK_DIR/jobs.csv"
cat >"$TRACE" <<'EOF'
job_submit_time,num_nodes,time_limit
0,2,10
5,1,10
EOF

# 1. --infile alone must run and produce the same schedule as the positional
#    form. This was the documented usage, and it used to exit with the usage
#    text because a positional argument was unconditionally required.
if "$SIMULATOR" "$TRACE" --total_nodes 2 \
        --outfile "$WORK_DIR/positional.out" >/dev/null 2>&1 \
   && "$SIMULATOR" --infile "$TRACE" --total_nodes 2 \
        --outfile "$WORK_DIR/option.out" >/dev/null 2>&1 \
   && cmp -s "$WORK_DIR/positional.out" "$WORK_DIR/option.out"; then
    echo "  ✓ --infile alone runs and matches the positional form"
    PASS=$((PASS + 1))
else
    echo "  ✗ --infile alone failed or produced a different schedule"
    FAIL=$((FAIL + 1))
fi

# 2. Giving both forms is an error, matching the tracer's rule, so neither
#    file is silently ignored.
if "$SIMULATOR" --infile "$TRACE" "$TRACE" --total_nodes 2 \
        >"$WORK_DIR/both.log" 2>&1; then
    echo "  ✗ --infile together with a positional argument was accepted"
    FAIL=$((FAIL + 1))
elif grep -q "not both" "$WORK_DIR/both.log"; then
    echo "  ✓ --infile together with a positional argument is rejected"
    PASS=$((PASS + 1))
else
    echo "  ✗ --infile plus positional: rejected, but message missing"
    FAIL=$((FAIL + 1))
fi

# 3. No input at all still prints usage and fails.
if "$SIMULATOR" --total_nodes 2 >"$WORK_DIR/none.log" 2>&1; then
    echo "  ✗ a command with no input trace was accepted"
    FAIL=$((FAIL + 1))
elif grep -q "Usage:" "$WORK_DIR/none.log"; then
    echo "  ✓ a command with no input trace prints usage and fails"
    PASS=$((PASS + 1))
else
    echo "  ✗ a command with no input trace failed without usage text"
    FAIL=$((FAIL + 1))
fi

# 4. Unsigned options reject a sign, garbage, and values above the type's
#    maximum instead of wrapping (atoi) or aborting (uncaught stoull).
for BAD in "-1" "abc" "4294967296" "+5"; do
    if "$SIMULATOR" "$TRACE" --total_nodes "$BAD" >"$WORK_DIR/tn.log" 2>&1; then
        echo "  ✗ --total_nodes $BAD was accepted"
        FAIL=$((FAIL + 1))
    elif grep -q "expects a non-negative integer" "$WORK_DIR/tn.log"; then
        echo "  ✓ --total_nodes $BAD is rejected"
        PASS=$((PASS + 1))
    else
        echo "  ✗ --total_nodes $BAD: rejected, but message missing"
        FAIL=$((FAIL + 1))
    fi
done

# 5. A negative value in a trace row is an error, not a 4-billion-node job.
cat >"$WORK_DIR/negative.csv" <<'EOF'
job_submit_time,num_nodes,time_limit
0,1,-100
EOF
if "$SIMULATOR" "$WORK_DIR/negative.csv" --total_nodes 2 \
        >"$WORK_DIR/neg.log" 2>&1; then
    echo "  ✗ a negative time_limit in a trace row was accepted"
    FAIL=$((FAIL + 1))
elif grep -q "unsigned integer" "$WORK_DIR/neg.log"; then
    echo "  ✓ a negative time_limit in a trace row is rejected"
    PASS=$((PASS + 1))
else
    echo "  ✗ negative time_limit: rejected, but message missing"
    FAIL=$((FAIL + 1))
fi

# 6. Values above INT_MAX but within unsigned range are valid and must load;
#    the old stoi()-based parser threw out_of_range on them.
cat >"$WORK_DIR/large.csv" <<'EOF'
job_submit_time,num_nodes,time_limit
0,3000000000,10
EOF
if "$SIMULATOR" "$WORK_DIR/large.csv" --total_nodes 3000000000 \
        --outfile "$WORK_DIR/large.out" >"$WORK_DIR/large.log" 2>&1 \
   && grep -q "^0,0,10,3000000000" "$WORK_DIR/large.out"; then
    echo "  ✓ a 3000000000-node job loads and runs"
    PASS=$((PASS + 1))
else
    echo "  ✗ a 3000000000-node job failed to load or run"
    FAIL=$((FAIL + 1))
fi

# 7. An input whose name ends in ".out" cannot get the default output name
#    (it would overwrite itself). That used to escape as an uncaught
#    exception and abort; it must be an ordinary usage error, and an
#    explicit -o must still work.
cp "$TRACE" "$WORK_DIR/previous.out"
if "$SIMULATOR" "$WORK_DIR/previous.out" --total_nodes 2 \
        >"$WORK_DIR/dotout.log" 2>&1; then
    echo "  ✗ a .out input with no -o was accepted"
    FAIL=$((FAIL + 1))
elif grep -q "terminate called" "$WORK_DIR/dotout.log"; then
    echo "  ✗ a .out input with no -o aborted instead of failing cleanly"
    FAIL=$((FAIL + 1))
elif grep -q "pass -o/--outfile" "$WORK_DIR/dotout.log" \
     && "$SIMULATOR" "$WORK_DIR/previous.out" --total_nodes 2 \
            --outfile "$WORK_DIR/from_dotout.csv" >/dev/null 2>&1 \
     && cmp -s "$WORK_DIR/positional.out" "$WORK_DIR/from_dotout.csv"; then
    echo "  ✓ a .out input is rejected without -o and runs with it"
    PASS=$((PASS + 1))
else
    echo "  ✗ a .out input: unexpected message or -o run failed"
    FAIL=$((FAIL + 1))
fi

echo "CLI input tests: $PASS passed, $FAIL failed"
test "$FAIL" -eq 0
