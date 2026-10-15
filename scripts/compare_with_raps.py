#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################
"""Compare DR_EVT's FCFS/EASY schedule with the RAPS reference scheduler.

Runs the same job list through both simulators with the same node count,
FCFS ordering, EASY backfilling, and recorded run times, then joins per-job
start times.

    python3 scripts/compare_with_raps.py --input jobs.csv --total_nodes 128
    python3 scripts/compare_with_raps.py --input trace.swf --total_nodes 338 \
        --cores_per_node 1 --max_jobs 500

SWF input is converted here with the same rules the simulator applies
(tests/run_swf_tests.sh pins that the C++ loader and this mapping agree).

Expected agreement: identical start order, and RAPS start times equal to
dr_evt's plus a small non-negative lag. RAPS advances on a 1-second tick and
releases a job's nodes on the tick after it completes, so every completion a
job waited behind adds one second there; dr_evt is event-driven and reuses
nodes at the completion instant. Beyond that lag, the
schedules diverge at EASY backfill decisions: RAPS computes the head job's
reservation from running jobs sorted by time_limit (raps/schedulers/default.py,
find_backfill_job), whereas EASY, and dr_evt, use the projected end time
start + time_limit. When a long-limit job started early and a short-limit job
started late, RAPS's shadow time is later than the true one and it backfills
jobs that delay the head. On the CTC SP2 trace this appears from roughly the
200th job on. A job that dr_evt starts *later* than RAPS is therefore usually
RAPS backfilling against its later shadow; check the first such job by hand
(the report lists them) before treating it as a dr_evt defect. The
independent EASY oracle in this repository is scripts/python_reference_scheduler.py.
Manual tool: RAPS is not part of CI.
"""

import argparse
import csv
import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def swf_to_csv(swf_path, csv_path, cores_per_node):
    unix_start = 0.0
    max_nodes = max_procs = 0.0
    rows = []
    skipped = 0
    with open(swf_path) as f:
        for line in f:
            line = line.rstrip()
            if not line:
                continue
            if line.startswith(";"):
                key, _, value = line[1:].partition(":")
                key = key.strip().lower()
                # Leading number only: archives annotate values, e.g.
                # "; MaxNodes: 338 (batch partition; 512 total)".
                try:
                    v = float(value.split()[0])
                except (ValueError, IndexError):
                    continue
                if key == "unixstarttime":
                    unix_start = v
                elif key == "maxnodes":
                    max_nodes = v
                elif key == "maxprocs":
                    max_procs = v
                continue
            fields = line.split()
            if len(fields) < 18:
                raise SystemExit(f"{swf_path}: record has {len(fields)} fields, expected 18")
            submit, run, procs, req = (float(fields[1]), float(fields[3]),
                                       float(fields[4]), float(fields[8]))
            if procs <= 0 or run <= 0:
                skipped += 1
                continue
            rows.append((unix_start + submit, run, procs, req))
    cpn = cores_per_node
    if cpn <= 0:
        cpn = math.floor(max_procs / max_nodes) if max_nodes > 0 and max_procs >= max_nodes else 1
    rows.sort(key=lambda r: r[0])
    with open(csv_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["job_submit_time", "num_nodes", "q_id", "time_limit", "actual_run_time"])
        for submit, run, procs, req in rows:
            limit = math.ceil(req if req > 0 else run)
            w.writerow([int(submit) if submit.is_integer() else submit,
                        int(math.ceil(procs / cpn)), 1, limit, int(run) if run.is_integer() else run])
    print(f"swf: {len(rows)} jobs written, {skipped} skipped, cores_per_node={cpn}")
    return csv_path


def clamp_actual(src, dst, max_jobs):
    """RAPS kills at time_limit; feed both simulators actual <= limit."""
    with open(src, newline="") as f, open(dst, "w", newline="") as out:
        reader = csv.DictReader(f)
        fields = reader.fieldnames
        if "actual_run_time" not in fields:
            fields = list(fields) + ["actual_run_time"]
        w = csv.DictWriter(out, fieldnames=fields)
        w.writeheader()
        n = 0
        for row in reader:
            limit = float(row["time_limit"])
            actual = float(row.get("actual_run_time") or limit)
            row["actual_run_time"] = min(actual, limit)
            w.writerow(row)
            n += 1
            if max_jobs and n >= max_jobs:
                break
    return n


def read_dr_evt(out_csv):
    starts = []
    with open(out_csv, newline="") as f:
        for row in csv.DictReader(f):
            starts.append((float(row["job_submit_time"]), float(row["begin_time"]),
                           int(row["num_nodes"]), float(row["time_limit"])))
    return starts


def run_dr_evt(simulator, csv_path, total_nodes, out_csv):
    cmd = [simulator, str(csv_path), "--total_nodes", str(total_nodes),
           "--run_time_mode", "actual", "--priority_policy", "fcfs",
           "--backfill_policy", "easy", "--outfile", str(out_csv)]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return read_dr_evt(out_csv)


def run_raps(raps_root, csv_path, total_nodes, horizon_s, out_dir, timeout_s):
    python = raps_root / "venv" / "bin" / "python"
    if not python.exists():
        raise SystemExit(f"RAPS venv python not found: {python}")
    env = dict(os.environ)
    env["PYTHONPATH"] = str(ROOT / "scripts") + os.pathsep + env.get("PYTHONPATH", "")
    raps_args = ["run",
           "--replay", str(csv_path), "--dataloader", "raps_dataloader_dr_evt",
           "--policy", "fcfs", "--backfill", "easy",
           "--system.base", "frontier",
           "--system.system.num-cdus", "1", "--system.system.racks-per-cdu", "1",
           "--system.system.nodes-per-rack", str(total_nodes),
           # One chassis, one node per blade: keeps RAPS's rack/power layout
           # consistent for any node count (it reshapes per-node arrays by
           # chassis and blade; Frontier's 8x2 layout only divides multiples
           # of 16). Power results are irrelevant to this comparison.
           "--system.system.chassis-per-rack", "1",
           "--system.system.nodes-per-blade", "1",
           "--system.system.missing-racks", "[]", "--system.system.down-nodes", "[]",
           "-t", f"{horizon_s}s", "-o", str(out_dir), "--noui", "--no-cooling"]
    # Run through a small wrapper so a hung RAPS prints its Python stack and
    # exits after `timeout_s` instead of blocking this script forever.
    wrapper = ("import faulthandler, runpy, sys\n"
               f"faulthandler.dump_traceback_later({int(timeout_s)}, exit=True)\n"
               "sys.argv = ['main.py'] + sys.argv[1:]\n"
               "runpy.run_path('main.py', run_name='__main__')\n")
    cmd = [str(python), "-c", wrapper] + raps_args
    # RAPS reads its optional experiment config from stdin when none is
    # given, so stdin must be closed or it blocks forever under a pipe.
    proc = subprocess.run(cmd, cwd=raps_root, env=env, text=True,
                          stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout[-4000:])
        raise SystemExit(f"RAPS exited with {proc.returncode}")
    hist = Path(out_dir) / "job_history.csv"
    if not hist.exists():
        sys.stderr.write(proc.stdout[-2000:])
        raise SystemExit(f"RAPS produced no {hist}")
    starts = {}
    with open(hist, newline="") as f:
        for row in csv.DictReader(f):
            starts[int(row["id"])] = (float(row["submit_time"]), float(row["start_time"]))
    return starts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", required=True, help="simple-format CSV or .swf file")
    ap.add_argument("--total_nodes", type=int, required=True)
    ap.add_argument("--cores_per_node", type=int, default=0, help="SWF only; 0 = header")
    ap.add_argument("--max_jobs", type=int, default=0, help="use only the first N jobs")
    ap.add_argument("--simulator", default=os.environ.get("DR_EVT_SIMULATOR", str(ROOT / "install" / "bin" / "simulator")))
    ap.add_argument("--raps-root", default=str(ROOT / "external" / "RAPS"))
    ap.add_argument("--workdir", default=None, help="keep intermediate files here")
    ap.add_argument("--tolerance", type=float, default=1.0, help="seconds counted as agreement")
    ap.add_argument("--raps-timeout", type=float, default=1800,
                    help="seconds before a stuck RAPS run is dumped and killed")
    ap.add_argument("--prepare-only", action="store_true",
                    help="write <workdir>/jobs.csv and stop (run the simulator elsewhere)")
    ap.add_argument("--dr-evt-output", default=None,
                    help="existing simulator --outfile for <workdir>/jobs.csv; skips running it")
    args = ap.parse_args()

    work = Path(args.workdir) if args.workdir else Path(tempfile.mkdtemp(prefix="dr_evt_raps_"))
    work.mkdir(parents=True, exist_ok=True)
    src = Path(args.input)
    if src.suffix.lower() == ".swf":
        src = Path(swf_to_csv(src, work / "from_swf.csv", args.cores_per_node))
    jobs_csv = work / "jobs.csv"
    n = clamp_actual(src, jobs_csv, args.max_jobs)
    print(f"jobs: {n}")
    if args.prepare_only:
        print(f"prepared {jobs_csv}; run the simulator on it with --total_nodes "
              f"{args.total_nodes} --run_time_mode actual --priority_policy fcfs "
              f"--backfill_policy easy, then rerun with --dr-evt-output")
        return 0

    if args.dr_evt_output:
        dr = read_dr_evt(args.dr_evt_output)
    else:
        dr = run_dr_evt(args.simulator, jobs_csv, args.total_nodes, work / "dr_evt.out.csv")
    t0 = min(s for s, _, _, _ in dr)
    horizon = int(max(b + l for _, b, _, l in dr) - t0) + 2
    raps = run_raps(Path(args.raps_root), jobs_csv, args.total_nodes, horizon, work / "raps",
                    args.raps_timeout)

    diffs = []
    missing = 0
    for i, (submit, begin, nodes, limit) in enumerate(dr):
        if i not in raps:
            missing += 1
            continue
        d = (begin - t0) - raps[i][1]
        diffs.append((abs(d), d, i, submit - t0, nodes, limit))
    within = sum(1 for a, *_ in diffs if a <= args.tolerance)
    exact = sum(1 for a, *_ in diffs if a == 0)
    # RAPS frees a job's nodes on the tick after it completes, so a job that
    # waited behind k completions starts up to k seconds later there. That
    # lag is always non-negative; dr_evt starting *later* than RAPS, or the
    # two simulators starting jobs in a different order, is what would point
    # at a real scheduling difference.
    dr_evt_later = sum(1 for a, d, *_ in diffs if d > 0)
    dr_order = [i for _, _, i, *_ in sorted(diffs, key=lambda x: (x[3] + 0, x[2]))]
    dr_starts = {i: dr[i][1] - t0 for _, _, i, *_ in diffs}
    raps_starts = {i: raps[i][1] for _, _, i, *_ in diffs}
    ids = sorted(dr_starts)
    inversions = 0
    for x in range(len(ids)):
        for y in range(x + 1, len(ids)):
            a, b = ids[x], ids[y]
            da = dr_starts[a] - dr_starts[b]
            ra = raps_starts[a] - raps_starts[b]
            if (da < 0 < ra) or (ra < 0 < da):
                inversions += 1
    print(f"compared {len(diffs)} jobs ({missing} missing from RAPS history)")
    print(f"exact start-time matches: {exact}; within {args.tolerance:g} s: {within}")
    print(f"jobs dr_evt starts later than RAPS: {dr_evt_later} "
          "(0 until RAPS's time_limit-sorted shadow lets it backfill past the head)")
    print(f"start-order inversions between the two schedules: {inversions}")
    if diffs:
        print(f"max |delta| = {max(a for a, *_ in diffs):.3f} s, "
              f"mean |delta| = {sum(a for a, *_ in diffs) / len(diffs):.3f} s")
        worst = sorted(diffs, reverse=True)[:10]
        if worst[0][0] > args.tolerance:
            print("worst disagreements (dr_evt - raps, seconds):")
            for a, d, i, submit, nodes, limit in worst:
                if a <= args.tolerance:
                    break
                print(f"  job {i}: delta={d:+.1f} submit={submit:.0f} nodes={nodes} limit={limit:.0f}")
    print(f"work dir: {work}")
    return 0 if diffs and dr_evt_later == 0 and inversions == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
