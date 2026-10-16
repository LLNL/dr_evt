# Tutorial: Your First Simulation

This tutorial walks you through running your first DR_EVT simulation.

## Step 1: Prepare a Test Trace

Create a simple test trace with 3 jobs:

```bash
cat > my_first_trace.csv << EOF
job_submit_time,num_nodes,time_limit
0,80,100
10,30,50
20,15,30
EOF
```

**What this means:**

- Job 0: Arrives at t=0, needs 80 nodes, duration 100 seconds
- Job 1: Arrives at t=10, needs 30 nodes, duration 50 seconds
- Job 2: Arrives at t=20, needs 15 nodes, duration 30 seconds

## Step 2: Run the Simulation

```bash
${CMAKE_INSTALL_PREFIX}/bin/simulator my_first_trace.csv \
  --total_nodes 100 \
  --trace_format simple \
  --timestamp_format epoch \
  --run_time_mode limit \
  --backfill_policy easy \
  --outfile results.csv
```

## Step 3: Understand the Output

The simulator's statistics summary includes:

```
=== Simulation Statistics ===
Total jobs: 3
Jobs submitted: 3
Jobs started: 3
Jobs completed: 3
Current time: 150
Total nodes: 100
Average wait time: 30 sec
Average turnaround time: 90 sec
Makespan: 150 sec
Average queue length: 0.333333 jobs
Peak queue length: 1 jobs
```

## Step 4: Analyze Results

View the output file:

```bash
cat results.csv
```

Expected output:
```text
job_submit_time,begin_time,end_time,num_nodes,exit_status,time_limit
0,0,100,80,0,100
10,100,150,30,0,50
20,20,50,15,0,30
```

**What happened:**

1. **t=0**: Job 0 starts (80 nodes)
2. **t=10**: Job 1 arrives but must wait (needs 30 nodes, only 20 are free)
3. **t=20**: Job 2 arrives and **backfills** (15 nodes fit and it can finish
   before Job 1's reservation)
4. **t=50**: Job 2 completes
5. **t=100**: Job 0 completes, and Job 1 starts
6. **t=150**: Job 1 completes

## Step 5: Visualize (Optional)

Create a simple visualization:

```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('results.csv')

fig, ax = plt.subplots(figsize=(10, 4))
for idx, row in df.iterrows():
    ax.barh(idx, row['end_time'] - row['begin_time'], 
            left=row['begin_time'], height=0.5,
            label=f"Job {idx} ({row['num_nodes']} nodes)")

ax.set_xlabel('Time (seconds)')
ax.set_ylabel('Job')
ax.set_title('Job Schedule Timeline')
ax.legend()
plt.savefig('timeline.png')
```

## Understanding Backfilling

In this example, Job 2 **backfilled**:

- Job 1 was waiting for Job 0 to complete (FCFS head)
- Job 2 arrived later but was small enough to fit
- Job 2 would complete (t=50) before Job 1's reservation (t=100)
- So Job 2 ran ahead of Job 1

This is **EASY backfilling** - it improves system utilization without delaying the waiting job.

## Next Steps

- [Command-Line Options](../user-guide/command-line.md) - All available options
- [Input Trace Files](../user-guide/trace-formats.md) - Input schemas
- [Backfilling Algorithms](../BACKFILLING_ALGORITHMS.md) - EASY and CONSERVATIVE specifications
- [Testing Guide](../TESTING_GUIDE.md) - How we verify correctness

## Exercises

Try these modifications:

1. **Add more jobs** - What happens with 10 jobs?
2. **Change resources** - Use `--total_nodes 50` - does Job 2 still backfill?
3. **Different durations** - Make Job 2 duration 100 instead of 30
4. **Real trace** - Try one of the test traces in
   `tests/test_traces/scheduler_correctness/`
