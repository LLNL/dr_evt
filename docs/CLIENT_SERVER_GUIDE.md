# gRPC Client/Server Guide and API

## Overview

DR_EVT's [streaming API](api/STREAMING_API.md) (`append_job()`/`append_jobs()`,
`advance_to()`, and the monitoring/statistics methods) lets
external code feed genuinely new jobs incrementally and control simulation
time advancement, rather than loading a full trace and running it
start-to-finish in one call. The gRPC client/server exposes that same
streaming API over the network: a `dr_evt_server` process holds one
`Simulation` instance per connected session, and any number of
`dr_evt_client` processes (or your own gRPC client, in any language gRPC
supports) can drive it remotely.

This is separate from, and does not replace, the plain CLI `simulator`
binary (batch mode) or the Python bindings (in-process streaming API). Use
the gRPC client/server specifically when the thing feeding jobs needs to
run in a different process - or on a different machine - than the
simulation itself.

Build and connection instructions are in
[Client/Server Setup](user-guide/grpc-setup.md). Dependency discovery is
documented in [Installation](getting-started/installation.md).

## The service definition

`src/proto/dr_evt_service.proto` defines a single bidirectional-streaming
RPC (`SimulationService.Session`), wrapping the same operations available
in-process via the streaming API:

| Request | Corresponds to |
|---|---|
| `InitRequest` | Constructing a `Simulation` from a `Sim_Params`-equivalent config |
| `RunRequest` | Calls `Simulation::run()`; initialized input and `sim_start_time` select simulation, full replay, or replay-based warm start, and configured `max_time` supplies an inclusive stopping boundary |
| `InitializeTraceRequest` | `Simulation::initialize_trace()` |
| `AppendJobRequest` | `Simulation::append_job()` - a genuinely new job, optionally including its known `actual_run_time` |
| `AppendJobsRequest` | `Simulation::append_jobs()` - the batch counterpart, with an optional known runtime per job |
| `GetJobStatusesRequest` | Query lifecycle and timing for IDs returned by the append APIs |
| `AdvanceToRequest` | `Simulation::advance_to()` |
| `RunUntilExclusiveRequest` | `Simulation::run_until_exclusive()` |
| `GetFCFSHeadShadowTimeRequest` | FCFS-head shadow time only: the earliest reserved start time, or `-1` with no waiting head |
| `GetBackfillWindowRequest` | One FCFS/EASY reservation snapshot: current capacity, shadow time, and projected releases |
| `GetPredictionHorizonRequest` | On-demand FCFS/EASY waiting-resource-time horizon after the shadow time |
| `SaveCheckpointRequest` | Return the current simulation state as binary checkpoint bytes |
| `LoadCheckpointRequest` | Replace the current simulation state from compatible checkpoint bytes |
| `GetStatisticsRequest`, `GetCurrentTimeRequest`, etc. | The monitoring/statistics methods |

`GetJobStatusesRequest.job_idx` accepts multiple appended-job IDs and returns
one `JobStatus` per ID in the same order. A pending job carries
`expected_start_time`; a running or completed job carries `scheduled` with
`start_time` and `end_time`; and a rejected job carries no timing value.
Unknown or non-appended IDs produce `ErrorResponse`. The request reads the
server's simulation state and works regardless of whether Redis output support
is compiled or configured.

Checkpoint requests use Ser20 and require a server built with
`DR_EVT_WITH_SER20=ON`; otherwise the server returns an error response. Save
and load archive configured file or Redis output independently of whether
status queries are used. File output is archived into numbered pre-restart
segments on load; use
`dr_evt_stitch_checkpoint_output` after the resumed run to reconstruct output
at the checkpoint boundary. For Redis, pass `--redis-uri` and `--redis-prefix`
to rebuild the canonical namespace.

For a replay-based warm start, set `InitRequest.sim_start_time` to a positive
global simulation boundary, provide a replay-format `infile`, and send
`RunRequest`.
This field is distinct from each job's historical `begin_time`. The server then
applies the same two-stage warm-start classification as the CLI and Python
batch API. A zero simulation start time preserves ordinary batch behavior;
negative and non-finite values are rejected.

Every `ClientMessage` carries a `request_id`, echoed back on the matching
`ServerMessage`, so a client can correlate responses even if it pipelines
multiple in-flight requests. The provided `dr_evt_client` batch-appends jobs
but sends one protocol request at a time and waits for each response; the
protocol itself does not require that.

### FCFS/EASY shadow-time and resource-change queries

Send `GetFCFSHeadShadowTimeRequest` when only the FCFS queue head's earliest
reserved start time is needed. Its `GetFCFSHeadShadowTimeResponse.shadow_time`
is `-1` when no job is waiting.

Send `GetBackfillWindowRequest` when the resource-change times that lead to
that reservation are also needed. After submitting work and advancing the
simulation to the desired point in time, its matching
`GetBackfillWindowResponse` is an atomic scheduling snapshot with:

- `current_time`: the simulation time at which the snapshot was made.
- `available_nodes`: nodes free immediately at `current_time`.
- `shadow_time`: the earliest start time reserved for the FCFS queue head;
  `-1` if no job is waiting.
- `releases`: the resource-change-time query: chronologically ordered
  `ResourceRelease` events for every running job that ends after
  `current_time`. Each has its absolute simulation `time` and `nodes_released`;
  jobs ending at the same time are combined into one event.

The projection deliberately uses each running job's `time_limit`, not its
actual runtime. That is the same estimate used by the FCFS/EASY scheduler to
calculate `shadow_time`, so clients can safely use the response to evaluate
backfill candidates without seeing a conflicting reservation model. `releases`
may be nonempty even when no job is waiting; it is empty only when nothing is
running.

For example, with no free nodes, a 40-node job predicted to end at time 50,
a 60-node job predicted to end at time 100, and a 100-node FCFS head, the
response at time 0 has `shadow_time = 100` and releases `(50, 40)` and
`(100, 60)`.

Send `GetPredictionHorizonRequest` with a utilization factor in `[0, 1]` to
estimate how long the current waiting resource-time demand takes to drain after
the shadow time. Zero selects the fallback factor `1`. The server scans fields
already stored in supported FCFS queues only for this request; no prediction
state is maintained during normal scheduling.

Session initialization, completion, reuse, and shutdown are documented in
[Client/Server Setup](user-guide/grpc-setup.md#session-identity-and-completion).

## Examples, use cases, and tests

- The [example C++ client](https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_client.cpp)
  demonstrates the complete request sequence described in
  [Connecting the example client](user-guide/grpc-setup.md#connecting-the-example-client).
  Its optional Redis mode advances to a requested time, pipelines finalized
  job lookups, queries the server only for missing IDs, and reports the merged
  statuses in append order.
- [Client/Server Use Cases](user-guide/client-server-use-cases.md) links the
  Python multi-server, MPI-launcher, and synchronized-system examples.
- The [distributed client/server tests](https://github.com/LLNL/dr_evt/blob/main/tests/README.md#distributed-clientserver-tests)
  section lists the exact gRPC and MPI test commands, fixtures, and coverage.
