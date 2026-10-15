# Input Trace Files

DR_EVT supports multiple input formats, trace data models, timestamp styles,
and timezone handling.

Use `--trace_format` to select the CSV parser and `--trace_type` to select the
job and resource data model. These options are independent. Their syntax and
defaults are in [Command-Line Options](command-line.md#trace-format-options).

The `standard` data model contains scheduling and resource fields. The `pcon`
model adds per-job `avgpcon`, `minpcon`, and `maxpcon` values and corresponding
resource-trace columns. The `swf` model reads the Standard Workload Format
directly; see [SWF Format](#swf-format).

The first data row of each input file is used to detect Unix epoch seconds or
calendar timestamps, and that encoding is then used for every timestamp in
the file. Mixed encodings in one file are rejected. `--timestamp_format` does
not enforce an encoding. Calendar input without an embedded UTC offset uses
the timezone selected by `--timezone`.
Simulator output is numeric regardless of `--timestamp_format`; use
`--msec_output` to retain milliseconds. Embedded numeric offsets have a known
conversion limitation described in
[Timezone Support](../dev/design-decisions/TIMEZONE_SUPPORT.md).

## Simple Format CSV Structure

### Simulation Mode (scheduler computes start/end times)
```text
job_submit_time,num_nodes,time_limit
0,10,100
50,10,50
120,10,80
```

To replay observed execution lengths while still letting the scheduler compute
new start and end times, include `actual_run_time`:

```text
job_submit_time,num_nodes,time_limit,actual_run_time
0,10,100,75
50,10,50,30
120,10,80,60
```

Run this simulation-format trace with `--run_time_mode actual`. The scheduler
uses `time_limit` for reservation planning, while each job releases its nodes
after `actual_run_time` seconds:

```bash
simulator jobs.csv --trace_format simple --timestamp_format epoch \
  --run_time_mode actual --total_nodes 100
```

### Replay Mode, With Epoch Timestamps
```text
job_submit_time,begin_time,end_time,num_nodes,time_limit
0,0,100,10,100
50,100,150,10,50
120,150,230,10,80
```

### Replay Mode, With ISO Timestamps
```text
job_submit_time,begin_time,end_time,num_nodes,time_limit
2024-01-15T00:00:00,2024-01-15T00:00:00,2024-01-15T00:01:40,10,100
2024-01-15T00:00:50,2024-01-15T00:01:40,2024-01-15T00:02:30,10,50
2024-01-15T00:02:00,2024-01-15T00:02:30,2024-01-15T00:03:50,10,80
```

### Power-Usage Simulation Mode
```text
job_submit_time,num_nodes,time_limit,avgpcon,minpcon,maxpcon
0,2,3,2.0,1.5,3.0
0,1,1,1.0,0.5,1.5
```

Use this form with `--trace_type pcon`. `q_id` remains optional and defaults
to `1` (`Queue1`). The `avgpcon`, `minpcon`, and `maxpcon` columns are required
and recognized only when `--trace_type pcon` is selected. With the default
`--trace_type standard`, they are treated as unrecognized extra columns and
ignored.

## Column Descriptions

### Simple Format Columns

Columns are looked up by name in the header row, not by fixed position -
any order works, and which of `begin_time`/`end_time` are present
determines simulation vs replay mode (see below).

| Name | Description | Required for |
|------|-------------|--------------|
| `job_submit_time` | When the job arrives/submits. Accepted alias: `submit_time` | Both modes |
| `num_nodes` | Number of nodes requested | Both modes |
| `q_id` | Optional one-based queue ID. If absent, the job uses `1` (`Queue1`). | Both modes |
| `time_limit` | Positive whole-number time limit (seconds); fractional values are rejected. Accepted column-name aliases: `time_limit`, `timelimit`, `walltime` | Both modes |
| `begin_time` | Historical start time of this individual job; distinct from the global `--sim_start_time` boundary | Replay mode only; must appear together with `end_time` |
| `end_time` | Historical end time from trace | Replay mode only; must appear together with `begin_time` |
| `avgpcon` | Average power usage associated with the job | Required only with `--trace_type pcon`; ignored in standard mode |
| `minpcon` | Minimum power usage associated with the job | Required only with `--trace_type pcon`; ignored in standard mode |
| `maxpcon` | Maximum power usage associated with the job | Required only with `--trace_type pcon`; ignored in standard mode |
| `exit_status` | Output-only compatibility field. The simulator currently writes `0`. | Generated output only |
| `actual_run_time` | The job's real, historical run time in seconds. It determines execution only for simulation `--run_time_mode actual`; in replay mode, `begin_time` and `end_time` determine execution and this field is only checked for consistency. Accepted column names: `actual_run_time`, `actual_runtime`, `duration`, `actual_duration`, `run_time` | Required for simulation `--run_time_mode actual`; optional in replay mode |

The complete simple-format alias sets are:

| Logical field | Accepted column names, in precedence order |
|---|---|
| Submission time | `job_submit_time`, `submit_time` |
| Time limit | `time_limit`, `timelimit`, `walltime` |
| Actual run time | `actual_run_time`, `actual_runtime`, `duration`, `actual_duration`, `run_time` |

If multiple aliases for one logical field are present, the first listed match
is used. These aliases apply to simple-format simulation and replay input.
Lassen input uses fixed column positions instead of header names.

A supplied `actual_run_time` must be finite. In simulation input it must not
exceed `time_limit`. In replay input it must equal `end_time - begin_time`
within `1e-6` seconds; if omitted, that timestamp difference supplies the
runtime. Invalid rows are diagnosed and skipped while the rest of the trace is
loaded.

**Ignored input columns**: input fields not used by the selected trace format
are ignored. In particular, `exit_status` is accepted only so a generated
simulator output can be used as replay input; its value is never read or used
to affect scheduling or replay. In the default ID-input build, `queue` is
ignored; `q_id` is optional and defaults to `1` (`Queue1`). Legacy named
`queue` input is available only with `-DDR_EVT_LEGACY_QUEUE_INPUT=ON`; in
that build `q_id` is ignored and an absent `queue` also defaults to `Queue1`.
Numeric `q_id` values are site-neutral and do not imply legacy queue behavior
such as `pAll` dedicated-allocation-time detection.

**TODO — user-defined queue names:** Allow users to define the accepted input
queue names and preserve those names in output.

**Mode detection - simulation vs replay**:
- No `begin_time`/`end_time` columns present → **simulation mode**: the
  scheduler computes start times; how the job's actual run time is
  determined is controlled separately by `--run_time_mode`
- `begin_time` and `end_time` present → **replay mode**: times are historical
  actuals, replayed exactly
- Exactly one of `begin_time` or `end_time` present is rejected as an
  ambiguous trace format

For implementation details, see
[Simulation vs Replay Modes](../dev/design-decisions/SIMULATION_VS_REPLAY_MODES.md).

### Lassen Format
33-column format specific to LLNL HPC traces. Columns used:
- Column 11: `num_nodes`
- Column 23: `begin_time`
- Column 24: `end_time`
- Column 29: `job_submit_time`
- Column 30: `queue`
- Column 32: `time_limit`

### SWF Format

The [Standard Workload Format](https://www.cs.huji.ac.il/labs/parallel/workload/swf.html)
used by the Parallel Workloads Archive is read natively with
`--trace_type swf` (equivalently `--trace_format swf`). SWF files have no
column header: lines beginning with `;` carry metadata, and every other line
holds 18 whitespace-separated numeric fields with `-1` for a missing value.

Header keys honoured:
- `UnixStartTime`: added to every relative submit time, so output timestamps
  are absolute epochs. Without it, times stay relative to the trace start.
- `MaxProcs` and `MaxNodes`: when both are present, the default
  processors-per-node divisor is `MaxProcs / MaxNodes`; otherwise `1`.
  `--cores_per_node N` overrides the header. Only the leading number of a
  header value is read, so annotations such as
  `; MaxNodes: 338 (batch partition; 512 total)` are accepted.

Field mapping (1-based SWF field numbers):

| SWF field | Simulator value |
|---|---|
| 2 submit time | `job_submit_time` = `UnixStartTime` + field 2 |
| 5 allocated processors | `num_nodes` = ceil(field 5 / cores_per_node) |
| 9 requested time | `time_limit`; when missing (`-1`) the run time (field 4) is used |
| 4 run time | `actual_run_time`, so `--run_time_mode actual` replays recorded durations; a run time longer than the requested time is truncated to it (the simulator kills jobs at their limit) and counted in the summary line |
| 1 job number, 15 queue | kept on the record for analysis; the scheduler's queue id is always `1` |

Records whose allocated processors or run time are `<= 0` (for example jobs
cancelled before they started) are skipped and counted; the loader prints
one summary line, `SWF: loaded X records, skipped Y ..., cores_per_node=C`.
The status field (11) is not consulted. Records are sorted by submit time
after loading, so ties and occasional out-of-order lines in archive files are
tolerated. SWF describes submissions only, so it always runs in simulation
mode; replay and warm start need `begin_time`/`end_time` and therefore a
simple-format trace.

When `--total_nodes` is not given, the header's `MaxNodes` is used as the
machine size (a note on stderr says so); without `MaxNodes` the built-in
default applies. An explicit `--total_nodes` always wins. A job whose node
count exceeds the machine size is rejected at submission, as for any format,
so keep `--total_nodes` and `--cores_per_node` consistent with the machine the
trace was recorded on.

SWF input is available to the command-line simulator and Protobuf
configuration files. The Python bindings and gRPC server use the standard
data model only, so convert SWF to simple-format CSV for those paths.

## See Also

- [User Guide](overview.md) - User guide overview
- [Testing Guide](../TESTING_GUIDE.md) - Test suite and validation
- [Quick Start](../getting-started/quickstart.md) - Getting started guide
- [Fugaku Power-Usage Experiment](fugaku-power-experiment.md) - Large-scale
  power-usage input and progressive loading example
