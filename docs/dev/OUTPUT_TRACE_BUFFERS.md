# Output-Trace Buffers and Streaming Trace State

`Trace` owns the state required to run a simulation and produce its output
traces. The two potentially unbounded collections—the job-record store and the
resource-history log—use `boost::circular_buffer`. This keeps their memory
behavior explicit while allowing a simulation to accept jobs incrementally.

`Simulation` owns a `Trace`; schedulers access job records through `Trace`
rather than retaining a pointer to a separate, fixed-layout job vector.

## The two circular buffers

| Buffer | Contents | When entries can be released | What happens when full |
| --- | --- | --- | --- |
| `Trace::m_data` | Job records used for scheduling, statistics, and the simulated-job trace | After the departure event has been processed and the schedule row has been consumed, or immediately for a rejected job | Write and reclaim the eligible front prefix; grow or abort if insertion still lacks space |
| `Trace::m_resource_history` | Finalized `(time, free_nodes, allocated_nodes)` resource samples | Immediately after the sample is recorded | Flush samples to the resource-trace file or structured Redis output and clear the buffer |

The buffers are independent of the scheduler's circular FCFS wait queue. The
CSV output files are streamed; they are not circular buffers themselves. See
[Output Trace Files](../user-guide/output-traces.md) for their formats and
defaults.

## Job-record storage and stable identifiers

Jobs retain their permanent `job_no` even as completed records are reclaimed
from the front of `m_data`. `Trace::job_at(job_no)` is the sole translation
point between that permanent identifier and the buffer's physical position:

```text
physical position = job_no - m_num_reclaimed
```

This works because job records are reclaimed only from the front and are never
compacted in the middle. A backfilled job may finish before an earlier job, but
it remains stored until every preceding record is also reclaimable. Schedulers
therefore hold a `Trace` reference and use `job_at()` rather than indexing the
buffer directly.

Before a record is discarded, `write_job_range()` consumes the eligible range
for the simulated job trace and accumulates summary statistics through
`write_job_line()`. A permanent job-number cursor tracks the next row to write
across physical `pop_front()` operations. The same range path is used by
reclamation and final output, so each scheduled job is written and counted
exactly once without adding a flag to every `Job_Record`.

For Redis output, the eligible const-iterator range is passed directly from
`m_data` to `RedisOutput`. Redis serializes that range in one transaction before
`pop_front()` invalidates it. No second job-record vector is retained. File
output continues to accumulate serialized CSV bytes in
`m_simulated_trace_buffer`.

Resource Redis output likewise consumes `m_resource_history` through const
iterators before clearing it; it does not copy samples into an output sidecar.

## Reclamation rules

Reclamation follows the same idea as a local Global Virtual Time (GVT)
boundary: completion makes a record eligible, but the record is not discarded
until all earlier records are also eligible and its output consumer has
advanced. `end_time <= current_time` supplies the time test; event-queue and
replay-enqueue boundaries ensure the corresponding departure accounting has
actually happened.

Reclamation is lazy and batched. It is not attempted after every completion.
It is triggered by:

- insertion pressure when the job store needs space;
- an explicit `flush_completed_jobs()` call;
- final output; or
- `--job_flush_interval` processed departures.

The interval is measured in records, not bytes. Its default value `0` follows
the current job-store capacity, which normally means capacity pressure is the
first reason to flush. Any other flush trigger resets the interval. One trigger
writes the whole completed contiguous front prefix as one output block.

- A job record becomes reclaimable after its departure event has run. The
  time-side check is `end_time <= current_time`; a pending event at the boundary
  prevents reclamation.
- A permanently unschedulable job is marked with
  `Job_Record::unscheduled_sentinel()` and can be reclaimed without waiting for
  an end time that will never exist.
- Resource samples need no delay. Once written to the resource-history buffer,
  no later simulation step needs to read them, so a full buffer can be flushed
  immediately.

For job records, the insertion order is reclaim first, then grow or abort only
if the required room is still unavailable. This permits a completed job's slot
to be reused without increasing capacity. The standalone replay tool does not
enable periodic reclamation because its later `print()`, `print_span()`, and
submission-statistics passes still consume the resident records.

## Loading and streaming jobs

There are three ways jobs enter `m_data`.

### Single-file batch loading

`load_data()` reads and sorts the whole input trace before simulation starts.
The job store grows to fit the entire file, even if a smaller initial
`--job_store_capacity` was requested. Consequently, that setting controls the
initial allocation in single-file mode, not the steady-state memory limit.

### Streaming API

`Trace::append_job()` and `Simulation::append_job()` add one newly arriving
job. Their gRPC counterpart is `AppendJobRequest`.

`Trace::append_jobs()` and `Simulation::append_jobs()` add a vector of newly
arriving jobs; their gRPC counterpart is `AppendJobsRequest`. A batch is
validated and given capacity before any record is appended:

1. Every submit time must be at least the current simulation time.
2. Submit times within the batch must be non-decreasing.
3. The store attempts to reclaim enough front records for the entire batch.
4. If necessary, capacity grows once to fit the batch, or the operation aborts
   before changing `m_data` when `--job_store_overflow=abort` is selected.

This makes a batch append all-or-nothing. `submit_job(job_no, ...)` is not an
append operation: it schedules a job record that is already present.

### Progressive multi-file loading

`--infile_list` supplies a pre-split, pre-sorted sequence of trace files.
`Trace::load_next_file()` reads one file at a time, and
`Simulation::run_progressive()` advances the simulation through that file
before loading the next. This avoids keeping all files' job records in memory
at once.

Each file must be sorted by `submit_time`, and the earliest job in a file must
not precede the latest job in the previous one. This is deliberately different
from adaptive chunking of a single input file: the caller chooses the file
boundaries.

## Capacity and memory-pressure checks

`resolve_job_store_capacity()` and the shared
`Trace::ensure_batch_capacity()` helper implement the job-store capacity
policy. The default job-store capacity is sized for the loaded job count;
resource history defaults to twice that count, because each job contributes at
most a start and an end resource sample. Both defaults have a minimum capacity
of 4096.

`--check_memory_pressure FRACTION` optionally prevents a capacity increase
whose projected peak allocation exceeds the requested fraction of available
memory. The check mirrors the actual doubling logic, including the temporary
overlap of the old and new circular buffers during `set_capacity()`. It also
accounts for the incoming batch. It is disabled unless explicitly set and is a
no-op when available memory cannot be determined (for example, outside Linux
or without `MemAvailable` in `/proc/meminfo`).

The check refuses the operation rather than waiting: a synchronous simulation
cannot obtain additional useful memory by waiting, and reclamation has already
been attempted before the check.

## Limits and future direction

Progressive loading is the memory-bounded option for known traces. Single-file
batch loading still reads the complete input file before the run begins. A
future adaptive single-file reader could choose chunks according to remaining
job-store capacity and memory pressure, but it is not required for the current
streaming or multi-file interfaces.

The reclamation-safety decision is isolated in `is_front_reclaimable()`. That
keeps the current single-process rule simple while leaving a natural extension
point for a future optimistic or parallel simulation horizon.

## Related code and tests

- `src/trace/trace.{hpp,cpp}`: storage, capacity, reclamation, and output
  writing
- `src/sim/sim.{hpp,cpp}`: streaming and progressive-run entry points
- `src/utils/system_memory.hpp`: Linux available-memory lookup
- [`tests/test_append_job_api.cpp`](https://github.com/LLNL/dr_evt/blob/main/tests/test_append_job_api.cpp): single and batched append behavior,
  including reclaim-before-grow and all-or-nothing capacity failures
- [`tests/test_grpc_streaming_api.cpp`](https://github.com/LLNL/dr_evt/blob/main/tests/test_grpc_streaming_api.cpp): gRPC streaming API
- [`tests/test_progressive_load.cpp`](https://github.com/LLNL/dr_evt/blob/main/tests/test_progressive_load.cpp)
  and [`tests/run_progressive_load_tests.sh`](https://github.com/LLNL/dr_evt/blob/main/tests/run_progressive_load_tests.sh):
  multi-file loading and memory-pressure behavior
