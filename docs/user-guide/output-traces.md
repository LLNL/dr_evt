# Output Trace Files

The simulator can write two CSV outputs for each run: a simulated-job schedule
and a resource-usage trace. Both currently write numeric timestamps regardless
of `--timestamp_format`. By default they are truncated to whole seconds;
`--msec_output` writes three decimal places. See
[Command-Line Options](command-line.md).

## Simulated-job schedule

Use `--outfile` to choose the simulated-job schedule file:

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator input.csv --outfile results/jobs_sim.csv
```

If `--outfile` is omitted, DR_EVT derives a filename from the input trace
(for example, `jobs.csv` becomes `jobs.out` in the current directory). Normally, the CSV contains
one row for each scheduled job. When `--max_time` is set, it contains only
jobs completed at or before that inclusive boundary; jobs still running or
waiting are omitted. In the default ID-input build, an input trace that
explicitly provides `q_id` retains that column:

```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,q_id,time_limit
```

When the selected queue field is omitted from input, it is omitted from
output too:

```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit
```

`begin_time` and `end_time` are the schedule produced by simulation.
`exit_status` is written as `0`. Jobs rejected before scheduling are not
written to this output. With
`-DDR_EVT_LEGACY_QUEUE_INPUT=ON`, the equivalent selected input/output column
is the legacy named `queue` field instead of `q_id`.

With `-DDR_EVT_WITH_REDIS=ON`, the schedule and resource history can instead be
written to Redis. See [Redis Output](redis-output.md) for server setup, flush
behavior, key layout, and queries.

**TODO — user-defined queue names:** Preserve user-defined input queue names
instead of emitting only DR_EVT's built-in canonical names.

## Resource-usage trace

With Redis output enabled, this CSV is stored at
`<redis_key_prefix>:resources:csv` rather than in a file.

Use `--resource_trace` to name the resource-usage file explicitly:

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator input.csv \
    --outfile results/jobs_sim.csv \
    --resource_trace results/resources.csv
```

The resource trace records the occupancy after each resource-state change.
With `--max_time`, no event after the inclusive boundary is recorded; if no
resource-state change occurs exactly at the boundary, no synthetic final row
is added there.
With the default `--trace_type standard`, the format is:

```text
time,free_nodes,allocated_nodes
0,100,0
0,20,80
10,5,95
40,20,80
100,100,0
```

With `--trace_type pcon`, the resource trace additionally reports the
quantities carried by the experimental power-usage trace model:

```text
time,free_nodes,allocated_nodes,avgpcon,minpcon,maxpcon
```

`--trace_type` selects the job/resource data model independently of
`--trace_format`, which selects how the input file is parsed.

Without `--resource_trace`, DR_EVT derives the name from the job output file:
a trailing `.csv` is replaced by `_resources.csv`, otherwise `_resources.csv`
is appended (for example, `results/jobs_sim.csv` becomes
`results/jobs_sim_resources.csv`, and the default `jobs.out` becomes
`jobs.out_resources.csv`).

## Replay outputs

The `tracer` uses the recorded `begin_time` and `end_time` values from its
input schedule to reconstruct the resource-usage trace described above when
`--resource_trace` is requested:

```bash
${CMAKE_INSTALL_PREFIX}/bin/tracer \
  --infile schedule.csv \
  --total_nodes 100 \
  --resource_trace replay-resources.csv
```

The following analysis reports are separate from the resource trace:

- `--outfile` optionally writes a CSV row for every job, including
  its wait time, execution time, and the busy-node count at submission. No
  per-job report is written when this option is omitted.
- `--subfile` writes the submission count and batch-resource availability for
  each hourly slot in the replay period.
- `--subsumf` aggregates submission and availability statistics for each of
  the 168 hour-of-week slots across the replayed weeks.
- `--datfile` optionally writes detected dedicated-allocation-time (DAT)
  periods as a text report.

The per-job and submission reports are CSV files. Each report is disabled when
its option is omitted. When neither submission report is requested, `tracer`
skips the submission-statistics pass entirely.

## CLI summary

At the end of a run, `simulator` prints the resolved configuration, job counts,
current simulation time, node count, average wait and turnaround times,
makespan, average and peak queue lengths, and process wall-clock time.

- **Wait time:** submission to start.
- **Turnaround time:** submission to completion.
- **Makespan:** first submission to last completion.
- **Average queue length:** number of jobs already waiting at each submission,
  averaged across submitted jobs. Running jobs are not counted.
- **Peak queue length:** largest number of waiting jobs during the run.

Buffer sizing and output-flush controls are documented in
[Command-Line Options](command-line.md).

## See also

- [Input Trace Files](trace-formats.md) - accepted input formats and columns
- [Command-Line Options](command-line.md) - `--outfile`, `--resource_trace`,
  `--job_flush_interval`, and `--resource_history_capacity`
- [Fugaku Power-Usage Experiment](fugaku-power-experiment.md) - Plotting node
  and power-usage resource curves
