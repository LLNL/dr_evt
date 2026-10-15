#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################
"""RAPS dataloader that reads a DR_EVT simple-format job CSV.

Used by scripts/compare_with_raps.py to run the RAPS reference scheduler on
exactly the job list DR_EVT simulated. Loaded through RAPS's own mechanism:

    raps run --replay jobs.csv --dataloader raps_dataloader_dr_evt ...

with this directory on PYTHONPATH. Only the standard library plus RAPS's own
modules are used. Expected columns: job_submit_time, num_nodes, time_limit,
actual_run_time (q_id is ignored).

Times handed to RAPS are integer seconds relative to the earliest submit time,
which is the convention RAPS's bundled dataloaders follow. expected_run_time is
clamped to time_limit because RAPS kills a job at its limit in policy mode.
"""

import csv
import math
from datetime import datetime, timezone

from raps.job import Job, job_dict
from raps.utils import WorkloadData

# Same value RAPS's bundled SWF loader uses; irrelevant without power traces.
TRACE_QUANTA = 15


def _read_rows(path):
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            yield row


def load_data(files, **kwargs):
    rows = []
    for path in files:
        rows.extend(_read_rows(path))
    if not rows:
        raise ValueError("no jobs found in " + ", ".join(files))

    t0 = min(float(r["job_submit_time"]) for r in rows)
    jobs = []
    span = 0
    max_limit = 0
    for i, r in enumerate(rows):
        submit = int(round(float(r["job_submit_time"]) - t0))
        nodes = int(r["num_nodes"])
        limit = int(math.ceil(float(r["time_limit"])))
        actual = r.get("actual_run_time")
        run = int(math.ceil(float(actual))) if actual not in (None, "") else limit
        run = max(1, min(run, limit))
        info = job_dict(
            id=i, name=str(i), account="dr_evt",
            nodes_required=nodes, submit_time=submit, time_limit=limit,
            expected_run_time=run, start_time=submit, end_time=submit + run,
            scheduled_nodes=[], cpu_trace=[], gpu_trace=[], ntx_trace=[],
            nrx_trace=[], trace_quanta=TRACE_QUANTA)
        jobs.append(Job(info))
        span = max(span, submit + run)
        max_limit = max(max_limit, limit)

    return WorkloadData(
        jobs=jobs, telemetry_start=0, telemetry_end=span + max_limit,
        start_date=datetime.fromtimestamp(t0, tz=timezone.utc))


def node_index_to_name(index, config=None):
    return str(index)
