# Python API

<div class="api-search" role="search">
  <label for="python-api-search">Search Python API</label>
  <input id="python-api-search" data-api-search="python-api-content" type="search" placeholder="e.g., SimParams, append_job, statistics" autocomplete="off">
  <span class="api-search-status" aria-live="polite"></span>
</div>

<div id="python-api-content" class="api-search-content">

The `dr_evt` extension exposes batch and incremental simulation through
pybind11. Build and import instructions are in
[Installation](../getting-started/installation.md#cmake-configuration-options).

## Example

```python
import dr_evt

params = dr_evt.SimParams()
params.infile = "jobs.csv"
params.total_nodes = 100
params.trace_format = "simple"
params.timestamp_format = "epoch"
params.run_time_mode = dr_evt.RunTimeMode.LIMIT

sim = dr_evt.Simulation(params)
queue = "pbatch" if dr_evt.legacy_queue_input else "1"
sim.append_job(0.0, 10, queue, 100.0)
sim.advance_to(0.0)

stats = sim.get_statistics()
print(stats.jobs_running, stats.nodes_in_use)
```

A complete runnable example is
[`python/example_streaming.py`](https://github.com/LLNL/dr_evt/blob/main/python/example_streaming.py).

For experimental external backfill selection, set
`params.num_max_candidates`, then call
`Simulation(params, job_cost_function, backfill_selector)`. The cost callback
receives `(job_id, submit_time, runtime_estimate, nodes_requested)`. The
selector receives a list of `(job_id, cost)` pairs and returns one offered ID
or `None`.

## Configuration

`SimParams` currently exposes these mutable attributes:

| Attribute | Type |
|---|---|
| `infile` | `str` |
| `redis_uri` | `str` |
| `redis_key_prefix` | `str` |
| `checkpoint_file` | `str` |
| `checkpoint_interval_jobs` | `int` |
| `total_nodes` | `int` |
| `capacity_schedule` | `str` |
| `sim_start_time` | `float` |
| `trace_format` | `str` |
| `timestamp_format` | `str` |
| `run_time_mode` | `RunTimeMode` |
| `backfill_policy` | `BackfillPolicy` |
| `num_max_candidates` | `int` |
| `priority_policy` | `PriorityPolicy` |
| `verbose` | `bool` |

`sim_start_time` is the global simulation boundary; it is distinct from each
job's historical `begin_time`. A positive value enables replay-based warm start
for replay input, while zero preserves ordinary full replay.

Set both Redis fields to direct the simulated-job schedule to the searchable
Redis schema and the resource history to `<redis_key_prefix>:resources:csv`, as
documented in [Redis Output](../user-guide/redis-output.md). The extension must
be built with `DR_EVT_WITH_REDIS=ON`.

Other C++/CLI configuration fields are not exposed by the binding. Use the
`simulator` executable when one of those settings is required; its options
are documented in [Command-Line Options](../user-guide/command-line.md).

The module exports `legacy_queue_input`, a Boolean indicating whether queue
arguments use legacy names or numeric IDs.

## Enumerations

- `RunTimeMode.ACTUAL`, `RunTimeMode.DISTRIBUTION`,
  `RunTimeMode.LIMIT`
- `BackfillPolicy.NONE`, `BackfillPolicy.EASY`,
  `BackfillPolicy.CONSERVATIVE`
- `PriorityPolicy.FCFS`, `PriorityPolicy.FCFS_CONSERVATIVE`,
  `PriorityPolicy.SJF`, `PriorityPolicy.LJF`
- `JobState.PENDING`, `JobState.RUNNING`, `JobState.COMPLETED`,
  `JobState.REJECTED` (returned by the job status query)

Their scheduling semantics are documented in
[Command-Line Options](../user-guide/command-line.md) and
[Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md).

## Simulation methods

| Method | Result |
|---|---|
| `run()` | Run a configured batch trace to completion. |
| `initialize_trace(max_jobs=0)` | Load the configured trace and return the number loaded. |
| `append_job(submit_time, num_nodes, queue, limit_time, actual_run_time=None)` | Append and enqueue one live job; return its ID. Jobs must be appended in non-decreasing `submit_time` order; the limit must be a positive whole number of seconds; a known runtime must be positive and no greater than the limit. |
| `append_jobs(requests)` | Atomically append and enqueue ordered `JobAppendRequest` values; return their IDs. |
| `get_job_statuses(job_idxs)` | Return lifecycle and timing snapshots for appended job IDs. |
| `advance_to(target_time)` | Process events at or before the target. |
| `run_until_exclusive(target_time)` | Process events strictly before the target. |
| `save_checkpoint(filename)` | Save complete simulation state to a same-build binary checkpoint. |
| `load_checkpoint(filename)` | Replace current state from a compatible checkpoint. |
| `get_current_time()` | Return current simulation time. |
| `get_nodes_in_use()` | Return allocated nodes. |
| `get_current_utilization()` | Return instantaneous usage of effective scheduled capacity. |
| `get_resource_area()` | Return Custom-FCFS allocated-node area in node-seconds; unavailable for standard schedulers. |
| `get_available_nodes()` | Return free nodes. |
| `get_active_job_count()` | Return waiting jobs. |
| `get_fcfs_head_shadow_time()` | Return the FCFS-head reservation time, or `-1`. |
| `get_backfill_window()` | Return the current FCFS/EASY reservation snapshot, including all running-job releases. |
| `get_prediction_horizon(utilization)` | On demand, estimate the supported FCFS/EASY waiting-queue drain time from the shadow time. |
| `get_statistics()` | Return a `Statistics` snapshot. |
| `write_simulated_trace()` | Write the configured job-schedule output. |
| `print_stats()` | Print summary statistics. |
| `get_trace_size()` | Return the number of records currently in the job store. |

Detailed time-advancement and job-submission contracts are defined in the
[Streaming API](STREAMING_API.md).

Checkpoint methods are present when DR_EVT is built with
`DR_EVT_WITH_SER20=ON`, and files require matching `SimParams`. Custom FCFS is
supported when the destination `Simulation` is constructed with equivalent
cost and selection callbacks; callback objects and external state are not
stored in the checkpoint. Standard, Pcon, and SWF trace state are preserved. Custom
scheduler subclasses and replay/warm-start are rejected rather than restored
inexactly. Progressive file loading resumes at the next fully admitted input
file. Output files remain external to the
checkpoint; loading archives them into numbered pre-restart segments and opens
fresh segments at the configured paths. Use `dr_evt_stitch_checkpoint_output`
after the resumed run to reconstruct each logical output. Redis output uses
numbered archived namespaces and the tool's `--redis-uri`/`--redis-prefix`
mode.

## Supporting types

`JobAppendRequest(submit_time, num_nodes, queue, limit_time,
actual_run_time=None)` represents one entry passed to `append_jobs()`. A known
actual runtime lets `RunTimeMode.ACTUAL` complete a streamed job before its
wall-time limit without any external per-job bookkeeping.

`get_job_statuses()` preserves the requested ID order (including duplicates).
A pending `JobStatus` has `state == JobState.PENDING` and an
`expected_start_time` projected from current capacity, running-job limit-time
releases, and the scheduler's current queue order. Running and completed jobs
have `start_time` and `end_time`; rejected jobs have no timing. The compact
timing record remains available after completed trace rows are flushed.

`BackfillWindow` exposes `current_time`, `available_nodes`,
`shadow_time`, and an ordered list of `ResourceRelease` values. Each release
contains `time` and `nodes_released`.

`Statistics` exposes:

- `jobs_submitted` (jobs accepted into the wait queue), `jobs_started`,
  `jobs_completed`, `jobs_running`, and `jobs_waiting`;
- `current_time`, `total_nodes`, `nodes_in_use`, and
  `nodes_available`; and
- `resource_area`, `utilization`, `avg_wait_time`, `avg_run_time`,
  `avg_turnaround_time`, `avg_bounded_slowdown`, and `makespan`.

`avg_bounded_slowdown` is the mean of
`max(1, turnaround / max(run_time, 10 seconds))` over completed jobs.

For simulations constructed with Custom-FCFS callbacks, `resource_area` is
accumulated as `nodes_in_use * interval` between settled scheduling times.
`utilization` divides that area by integrated effective capacity over the
accounting horizon (the snapshot time while work is running, or the last
resource event after it becomes idle). During non-preemptive draining,
effective capacity is at least the running allocation. Unlike
`get_current_utilization()`, intervals that are far apart therefore carry
proportionally more weight. Standard schedulers retain the post-hoc
scheduled-workload calculation and do not perform live area bookkeeping.

Metric definitions are in
[Output Trace Files](../user-guide/output-traces.md#cli-summary).

## Testing and source

The binding is defined in
[`python/dr_evt_bindings.cpp`](https://github.com/LLNL/dr_evt/blob/main/python/dr_evt_bindings.cpp).
Python test commands and status are maintained in the
[Python API tests section](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#python-api-tests)
of the Test Suite README. The test implementation is
[`tests/test_python_api.py`](https://github.com/LLNL/dr_evt/blob/main/tests/test_python_api.py).

</div>
