# Experimental trace types

DR_EVT supports specialized experimental job-trace data models without adding
experimental storage to standard jobs.

## Design

The trace implementation is selected through compile-time policy
specialization:

```text
BasicTrace<Standard_Trace_Policy> -> Trace
BasicTrace<Pcon_Trace_Policy>     -> PconTrace
BasicTrace<Swf_Trace_Policy>      -> SwfTrace

BasicSimulation<Trace>            -> Simulation
BasicSimulation<PconTrace>        -> PconSimulation
BasicSimulation<SwfTrace>         -> SwfSimulation
```

The command-line simulator chooses between these already-specialized concrete
simulation types once at startup. The trace-type choice is therefore not made
for each job.

The standard trace continues to store `Job_Record` directly. It does not add a
power-usage pointer, vector, variant, optional field, discriminator, or other
power-usage-specific per-job storage.

The power-usage trace instead stores `Pcon_Job_Record`, which derives from
`Job_Record` and carries three power-usage values inline:

```text
avgpcon
minpcon
maxpcon
```

This also avoids per-job allocation/deallocation for the experimental values.

## Command line

The trace data model is selected with:

```text
--trace_type standard
--trace_type pcon
--trace_type swf
```

`standard` is the default. `swf` reads Standard Workload Format files directly
and accepts `--cores_per_node` to convert processor counts to nodes.

`--trace_type` is independent of `--trace_format`. The former chooses the
in-memory job/resource data model; the latter chooses the input trace parser
and layout, such as `simple` or `lassen`.

For a simple power-usage scheduling trace, the input includes the normal
scheduling columns plus:

```text
avgpcon,minpcon,maxpcon
```

The power-usage resource trace adds those aggregate columns to the standard
resource history:

```text
time,free_nodes,allocated_nodes,avgpcon,minpcon,maxpcon
```

Selecting the standard trace type does not add power-usage fields to
`Job_Record` or to the standard resource-history sample, and introduces no per-job
experimental allocation or dynamic container.
