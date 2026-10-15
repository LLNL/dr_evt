#!/bin/bash
# Checks for Standard Workload Format (SWF) input (--trace_type swf): field
# mapping against an equivalent simple-format CSV, cores-per-node derivation
# and override, skipped-record accounting, malformed-line rejection, and the
# --trace_type/--trace_format consistency rule.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
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
    "${TMPDIR:-/tmp}/dr-evt-swf.XXXXXXXX" 2>/dev/null)"; then
    WORK_DIR="$(mktemp -d "/tmp/dr-evt-swf.XXXXXXXX")"
fi
cleanup() { rm -rf -- "$WORK_DIR"; }
test_report_set_cleanup cleanup

PASS=0
FAIL=0

FIX="$REPO_ROOT/tests/test_traces/swf"
COMMON=(--run_time_mode actual --priority_policy fcfs --backfill_policy easy)

# 1. The SWF fixture and its hand-converted simple-format CSV must produce
#    byte-identical schedules: this pins the field mapping (UnixStartTime +
#    relative submit, ceil(processors / cores_per_node), requested time with
#    run-time fallback, actual run time) and the file-local submit-time sort.
"$SIMULATOR" "$FIX/tiny.swf" --trace_type swf --total_nodes 4 "${COMMON[@]}" \
    --outfile "$WORK_DIR/swf.out" >"$WORK_DIR/swf.log" 2>&1
SWF_RC=$?
"$SIMULATOR" "$FIX/tiny_equiv.csv" --total_nodes 4 "${COMMON[@]}" \
    --outfile "$WORK_DIR/csv.out" >"$WORK_DIR/csv.log" 2>&1
if [ "$SWF_RC" -eq 0 ] && cmp -s "$WORK_DIR/swf.out" "$WORK_DIR/csv.out"; then
    echo "  ✓ SWF input schedules identically to its simple-format equivalent"
    PASS=$((PASS + 1))
else
    echo "  ✗ SWF input differs from its CSV equivalent (rc=$SWF_RC)"
    FAIL=$((FAIL + 1))
fi

# 2. The load summary reports the header-derived divisor and skipped rows:
#    MaxProcs 16 / MaxNodes 4 = 4; rows with run time 0 and processors -1.
if grep -q "cores_per_node=4" "$WORK_DIR/swf.log" \
   && grep -q "skipped 2 " "$WORK_DIR/swf.log"; then
    echo "  ✓ summary reports cores_per_node=4 and 2 skipped records"
    PASS=$((PASS + 1))
else
    echo "  ✗ summary lacks cores_per_node=4 or the skipped count"
    FAIL=$((FAIL + 1))
fi

# 3. --cores_per_node overrides the header: with 1 the 16-processor job
#    needs 16 nodes and is rejected against total_nodes 4.
if "$SIMULATOR" "$FIX/tiny.swf" --trace_type swf --cores_per_node 1 \
        --total_nodes 4 "${COMMON[@]}" --outfile "$WORK_DIR/cpn1.out" \
        >"$WORK_DIR/cpn1.log" 2>&1 \
   && grep -q "cores_per_node=1" "$WORK_DIR/cpn1.log" \
   && grep -q "requests 16 nodes" "$WORK_DIR/cpn1.log"; then
    echo "  ✓ --cores_per_node 1 overrides the header-derived divisor"
    PASS=$((PASS + 1))
else
    echo "  ✗ --cores_per_node 1 did not take effect"
    FAIL=$((FAIL + 1))
fi

# 4. Without header metadata the divisor is 1 and times stay relative.
"$SIMULATOR" "$FIX/tiny_noheader.swf" --trace_type swf --total_nodes 16 \
    "${COMMON[@]}" --outfile "$WORK_DIR/nh.out" >"$WORK_DIR/nh.log" 2>&1
"$SIMULATOR" "$FIX/tiny_noheader_equiv.csv" --total_nodes 16 "${COMMON[@]}" \
    --outfile "$WORK_DIR/nhc.out" >"$WORK_DIR/nhc.log" 2>&1
if grep -q "cores_per_node=1" "$WORK_DIR/nh.log" \
   && cmp -s "$WORK_DIR/nh.out" "$WORK_DIR/nhc.out"; then
    echo "  ✓ a header-less SWF file uses cores_per_node=1 and relative times"
    PASS=$((PASS + 1))
else
    echo "  ✗ header-less SWF handling differs from its equivalent CSV"
    FAIL=$((FAIL + 1))
fi

# 5. A record with too few fields is rejected with a line number.
if "$SIMULATOR" "$FIX/malformed.swf" --trace_type swf --total_nodes 4 \
        --outfile "$WORK_DIR/bad.out" >"$WORK_DIR/bad.log" 2>&1; then
    echo "  ✗ a 17-field SWF record was accepted"
    FAIL=$((FAIL + 1))
elif grep -q "expected 18" "$WORK_DIR/bad.log"; then
    echo "  ✓ a 17-field SWF record is rejected"
    PASS=$((PASS + 1))
else
    echo "  ✗ a 17-field SWF record failed without the expected message"
    FAIL=$((FAIL + 1))
fi

# 6. SWF has its own parser: combining it with a CSV column layout, or using
#    --cores_per_node without SWF input, is a usage error.
if "$SIMULATOR" "$FIX/tiny.swf" --trace_type swf --trace_format lassen \
        --total_nodes 4 >"$WORK_DIR/mix.log" 2>&1; then
    echo "  ✗ --trace_type swf with --trace_format lassen was accepted"
    FAIL=$((FAIL + 1))
elif "$SIMULATOR" "$FIX/tiny_equiv.csv" --cores_per_node 4 --total_nodes 4 \
        >"$WORK_DIR/cpn_csv.log" 2>&1; then
    echo "  ✗ --cores_per_node was accepted for non-SWF input"
    FAIL=$((FAIL + 1))
elif grep -q "go together" "$WORK_DIR/mix.log" \
     && grep -q "SWF input only" "$WORK_DIR/cpn_csv.log"; then
    echo "  ✓ SWF options are rejected when inconsistent with the input"
    PASS=$((PASS + 1))
else
    echo "  ✗ inconsistent SWF options: rejected, but message missing"
    FAIL=$((FAIL + 1))
fi

# 7. Without -n the machine size comes from the header's MaxNodes (4 here),
#    so the schedule equals the explicit --total_nodes 4 run.
if "$SIMULATOR" "$FIX/tiny.swf" --trace_type swf "${COMMON[@]}" \
        --outfile "$WORK_DIR/hdrn.out" >"$WORK_DIR/hdrn.log" 2>&1 \
   && grep -q "using MaxNodes from the header: 4" "$WORK_DIR/hdrn.log" \
   && cmp -s "$WORK_DIR/hdrn.out" "$WORK_DIR/swf.out"; then
    echo "  ✓ total_nodes defaults to the header's MaxNodes"
    PASS=$((PASS + 1))
else
    echo "  ✗ total_nodes did not default to MaxNodes"
    FAIL=$((FAIL + 1))
fi

# 8. No header: the built-in default is used, with a note.
if "$SIMULATOR" "$FIX/tiny_noheader.swf" --trace_type swf "${COMMON[@]}" \
        --outfile "$WORK_DIR/nhn.out" >"$WORK_DIR/nhn.log" 2>&1 \
   && grep -q "header has no MaxNodes; using the built-in default of 795" \
        "$WORK_DIR/nhn.log"; then
    echo "  ✓ without MaxNodes the built-in default applies, with a note"
    PASS=$((PASS + 1))
else
    echo "  ✗ missing-MaxNodes fallback did not behave as expected"
    FAIL=$((FAIL + 1))
fi

echo "SWF tests: $PASS passed, $FAIL failed"
test "$FAIL" -eq 0
