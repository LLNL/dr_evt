#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
DR_EVT Python API Test Suite

Tests all Python bindings including:
- Configuration parameters
- Streaming API
- Monitoring API
- Statistics
- Different scheduling policies

Note: Scheduler uses time_limit as the best estimator for planning.
run_time_mode is set to LIMIT so jobs run exactly their time_limit.
"""

import sys
import os
import subprocess
import tempfile

try:
    import dr_evt
except ImportError as e:
    print(f"✗ Failed to import dr_evt module: {e}", file=sys.stderr)
    print("\nBuild Python bindings with:", file=sys.stderr)
    print("  cmake -S . -B build -DDR_EVT_BUILD_PYTHON=ON", file=sys.stderr)
    print("  cmake --build build -j4", file=sys.stderr)
    sys.exit(1)

QUEUE_INPUT = "pbatch" if dr_evt.legacy_queue_input else "1"


class TestResult:
    """Track test results"""
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.errors = []

    def record_pass(self, test_name):
        self.passed += 1
        print(f"  ✓ {test_name}")

    def record_fail(self, test_name, error):
        self.failed += 1
        self.errors.append((test_name, error))
        print(f"  ✗ {test_name}: {error}")

    def summary(self):
        total = self.passed + self.failed
        print(f"\n{'='*60}")
        print(f"Test Results: {self.passed}/{total} passed")
        print(f"{'='*60}")

        if self.errors:
            print("\nFailed tests:")
            for name, error in self.errors:
                print(f"  - {name}: {error}")
            return 1
        else:
            print("\n✅ ALL PYTHON API TESTS PASSED!")
            return 0


def create_test_trace(filename, jobs):
    """Create a test trace file"""
    with open(filename, 'w') as f:
        f.write("job_submit_time,num_nodes,time_limit\n")
        for job in jobs:
            f.write(f"{job[0]},{job[1]},{job[2]}\n")


def create_replay_trace(filename):
    """Create a replay trace spanning a nonzero warm-start boundary."""
    with open(filename, 'w') as f:
        f.write("job_submit_time,begin_time,end_time,num_nodes,"
                "exit_status,time_limit\n")
        f.write("0,0,5,2,0,5\n")       # completed history
        f.write("1,2,10,3,0,8\n")      # ends exactly at t
        f.write("2,4,15,2,0,11\n")     # active history
        f.write("3,5,15,3,0,10\n")     # active history
        f.write("4,12,14,1,0,2\n")     # inherited waiter: excluded
        f.write("10,10,14,4,0,4\n")    # boundary arrival
        f.write("11,12,14,5,0,2\n")    # future arrival


def test_module_import(result):
    """Test 1: Module import and version"""
    print("\n1. Module Import")
    try:
        assert hasattr(dr_evt, '__version__')
        result.record_pass(f"Version: {dr_evt.__version__}")
    except Exception as e:
        result.record_fail("Module version", str(e))


def test_streaming_example_from_repo_root(result):
    """The documented invocation must not depend on the caller's cwd."""
    print("\n1b. Streaming Example")
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    try:
        completed = subprocess.run(
            [sys.executable, 'python/example_streaming.py'],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=False,
        )
        assert completed.returncode == 0, completed.stderr or completed.stdout
        assert 'Final Statistics' in completed.stdout
        result.record_pass("Streaming example from repository root")
    except Exception as e:
        result.record_fail("Streaming example from repository root", str(e))


def test_enumerations(result):
    """Test 2: Enumerations"""
    print("\n2. Enumerations")

    # BackfillPolicy
    try:
        assert hasattr(dr_evt, 'BackfillPolicy')
        assert hasattr(dr_evt.BackfillPolicy, 'NONE')
        assert hasattr(dr_evt.BackfillPolicy, 'EASY')
        assert hasattr(dr_evt.BackfillPolicy, 'CONSERVATIVE')
        result.record_pass("BackfillPolicy")
    except Exception as e:
        result.record_fail("BackfillPolicy", str(e))

    # PriorityPolicy
    try:
        assert hasattr(dr_evt, 'PriorityPolicy')
        assert hasattr(dr_evt.PriorityPolicy, 'FCFS')
        assert hasattr(dr_evt.PriorityPolicy, 'FCFS_CONSERVATIVE')
        assert hasattr(dr_evt.PriorityPolicy, 'SJF')
        assert hasattr(dr_evt.PriorityPolicy, 'LJF')
        result.record_pass("PriorityPolicy")
    except Exception as e:
        result.record_fail("PriorityPolicy", str(e))

    # RunTimeMode enum values
    try:
        assert hasattr(dr_evt, 'RunTimeMode')
        assert hasattr(dr_evt.RunTimeMode, 'ACTUAL')
        assert hasattr(dr_evt.RunTimeMode, 'DISTRIBUTION')
        assert hasattr(dr_evt.RunTimeMode, 'LIMIT')
        result.record_pass("RunTimeMode")
    except Exception as e:
        result.record_fail("RunTimeMode", str(e))


def test_sim_params(result):
    """Test 3: SimParams configuration"""
    print("\n3. SimParams Configuration")

    try:
        params = dr_evt.SimParams()

        # Test all exposed parameters
        params.infile = "test.csv"
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.LIMIT
        params.backfill_policy = dr_evt.BackfillPolicy.EASY
        params.num_max_candidates = 8
        params.priority_policy = dr_evt.PriorityPolicy.FCFS
        params.verbose = False

        result.record_pass("SimParams creation and configuration")
    except Exception as e:
        result.record_fail("SimParams", str(e))


def test_streaming_api(result):
    """Test 4: Streaming API"""
    print("\n4. Streaming API")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        # Create test trace
        create_test_trace(trace_file.name, [
            (0, 10, 100),    # Job 0: t=0, 10 nodes, 100s
            (50, 20, 100),   # Job 1: t=50, 20 nodes, 100s
        ])

        # Configure
        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.LIMIT
        params.backfill_policy = dr_evt.BackfillPolicy.EASY
        params.num_max_candidates = 2
        params.priority_policy = dr_evt.PriorityPolicy.FCFS

        # Create simulation
        sim = dr_evt.Simulation(params)
        # append_job() is the public streaming entry point.
        first_id = sim.append_job(0.0, 10, QUEUE_INPUT, 100)
        # Appended in submit-time order, as the streaming contract requires.
        third_id = sim.append_job(25.0, 5, QUEUE_INPUT, 10)
        second_id = sim.append_job(50.0, 20, QUEUE_INPUT, 100)
        pending = sim.get_job_statuses([second_id, first_id, second_id])
        assert [item.job_idx for item in pending] == [second_id, first_id,
                                                       second_id]
        assert pending[0].state == dr_evt.JobState.PENDING
        assert pending[0].expected_start_time == 50.0
        assert pending[1].expected_start_time == 0.0
        sim.advance_to(0.0)
        active = sim.get_job_statuses([first_id, second_id])
        assert active[0].state == dr_evt.JobState.RUNNING
        assert active[0].start_time == 0.0
        assert active[0].end_time == 100.0
        assert active[1].state == dr_evt.JobState.PENDING
        result.record_pass("append_job, status query, and advance_to")

        # Test run_until_exclusive: the job at t=25..35 lies strictly before
        # the target and must be processed (a no-op would leave the clock
        # at 0); the arrival at exactly t=50 must not be.
        sim.run_until_exclusive(50.0)
        assert sim.get_current_time() == 35.0, \
            f"run_until_exclusive(50.0) should stop at the t=35 end event, but time={sim.get_current_time()}"
        third = sim.get_job_statuses([third_id])[0]
        assert third.state == dr_evt.JobState.COMPLETED, \
            f"third job should be COMPLETED at t=35, got {third.state}"
        assert sim.get_nodes_in_use() == 10, \
            f"run_until_exclusive(50.0) should not process the t=50 event yet, but nodes_in_use={sim.get_nodes_in_use()}"
        sim.advance_to(50.0)
        assert sim.get_nodes_in_use() == 30
        result.record_pass("run_until_exclusive")

    except Exception as e:
        result.record_fail("Streaming API", str(e))
    finally:
        os.unlink(trace_file.name)


def test_monitoring_api(result):
    """Test 5: Monitoring API"""
    print("\n5. Monitoring API")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [(0, 30, 100)])

        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.LIMIT

        sim = dr_evt.Simulation(params)
        # Initial state
        assert sim.get_current_time() == 0.0
        assert sim.get_nodes_in_use() == 0
        assert sim.get_available_nodes() == 100
        try:
            sim.get_resource_area()
            raise AssertionError("standard scheduler exposed live resource area")
        except RuntimeError:
            pass
        result.record_pass("Initial state monitoring")

        # After job starts
        sim.append_job(0.0, 30, QUEUE_INPUT, 100)
        sim.advance_to(0.0)
        assert sim.get_nodes_in_use() == 30
        assert sim.get_available_nodes() == 70
        assert abs(sim.get_current_utilization() - 0.3) < 1e-12
        result.record_pass("Active state monitoring")

        # Queue status
        queue_size = sim.get_active_job_count()
        shadow_time = sim.get_fcfs_head_shadow_time()
        result.record_pass("Queue status API")

    except Exception as e:
        result.record_fail("Monitoring API", str(e))
    finally:
        os.unlink(trace_file.name)


def test_backfill_window_api(result):
    """The in-process Python API exposes the FCFS/EASY reservation snapshot."""
    print("\n5b. Backfill Window API")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        # The 100-node queue head must wait for both running jobs.  Their
        # time limits produce the same releases used for its EASY reservation.
        create_test_trace(trace_file.name, [
            (0, 40, 50),
            (0, 60, 100),
            (0, 100, 10),
        ])

        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.LIMIT
        params.backfill_policy = dr_evt.BackfillPolicy.EASY
        params.priority_policy = dr_evt.PriorityPolicy.FCFS

        sim = dr_evt.Simulation(
            params,
            lambda job_id, _submit, _runtime, _nodes: job_id,
            lambda candidates: candidates[0][0] if candidates else None,
        )
        for num_nodes, limit_time in [(40, 50), (60, 100), (100, 10)]:
            sim.append_job(0.0, num_nodes, QUEUE_INPUT, limit_time)
            sim.advance_to(0.0)

        window = sim.get_backfill_window()
        assert window.current_time == 0.0
        assert window.available_nodes == 0
        assert window.shadow_time == 100.0
        assert [(release.time, release.nodes_released) for release in window.releases] == [
            (50.0, 40), (100.0, 60)
        ]
        # No running-job completion remains after the shadow event, so the
        # 1000 node-seconds are drained at U * total_nodes = 50 nodes.
        assert abs(sim.get_prediction_horizon(0.5) - 20.0) < 1e-12
        # Once the waiting head starts, the snapshot continues to expose the
        # projected releases of running work.
        sim.advance_to(100.0)
        full_window = sim.get_backfill_window()
        assert full_window.shadow_time == -1.0
        assert [(release.time, release.nodes_released)
                for release in full_window.releases] == [(110.0, 100)]

        standard = dr_evt.Simulation(params)
        standard.append_job(0.0, 100, QUEUE_INPUT, 40.0)
        standard.advance_to(0.0)
        standard.append_job(0.0, 50, QUEUE_INPUT, 8.0)
        standard.advance_to(0.0)
        assert standard.get_prediction_horizon(0.5) == 8.0
        result.record_pass("Backfill window snapshot")
    except Exception as e:
        result.record_fail("Backfill window API", str(e))
    finally:
        os.unlink(trace_file.name)


def test_custom_backfill_api(result):
    """Cost and selection callbacks drive the custom EASY scheduler."""
    print("\n5c. Custom Backfill API")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [])
        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.LIMIT
        params.backfill_policy = dr_evt.BackfillPolicy.EASY
        params.num_max_candidates = 2

        costed_jobs = []
        candidate_windows = []

        def compute_cost(job_id, submit_time, runtime, nodes):
            costed_jobs.append(job_id)
            return job_id

        def select_lowest_cost(candidates):
            candidate_windows.append(candidates)
            return min(candidates, key=lambda candidate: candidate[1])[0]

        sim = dr_evt.Simulation(params, compute_cost, select_lowest_cost)
        for nodes, runtime in [(70, 100), (50, 200), (20, 50),
                               (10, 20), (10, 30)]:
            sim.append_job(0.0, nodes, QUEUE_INPUT, runtime)
        sim.advance_to(0.0)

        assert costed_jobs == [0, 1, 2, 3, 4]
        assert candidate_windows[0] == [(2, 2), (3, 3)]
        assert select_lowest_cost([(7, 4), (8, 2), (9, 2)]) == 8
        result.record_pass("Custom cost and selection callbacks")
    except Exception as e:
        result.record_fail("Custom backfill API", str(e))
    finally:
        os.unlink(trace_file.name)


def test_statistics(result):
    """Test 6: Statistics"""
    print("\n6. Statistics API")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [
            (0, 10, 50),
            (10, 20, 50),
            (20, 30, 50),
        ])

        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.LIMIT

        sim = dr_evt.Simulation(
            params,
            lambda job_id, _submit, _runtime, _nodes: job_id,
            lambda candidates: candidates[0][0] if candidates else None,
        )
        # Run complete simulation
        sim.advance_to(0.0)
        sim.append_job(0.0, 10, QUEUE_INPUT, 50)
        sim.advance_to(0.0)

        sim.append_job(10.0, 20, QUEUE_INPUT, 50)
        sim.advance_to(10.0)

        sim.append_job(20.0, 30, QUEUE_INPUT, 50)
        sim.advance_to(20.0)

        sim.advance_to(100.0)

        # Get statistics
        stats = sim.get_statistics()

        # Check all fields exist
        assert hasattr(stats, 'jobs_submitted')
        assert hasattr(stats, 'jobs_completed')
        assert hasattr(stats, 'jobs_running')
        assert hasattr(stats, 'jobs_waiting')
        assert hasattr(stats, 'current_time')
        assert hasattr(stats, 'total_nodes')
        assert hasattr(stats, 'nodes_in_use')
        assert hasattr(stats, 'nodes_available')
        assert hasattr(stats, 'resource_area')
        assert hasattr(stats, 'utilization')
        assert hasattr(stats, 'avg_wait_time')
        assert hasattr(stats, 'avg_turnaround_time')
        assert hasattr(stats, 'makespan')

        # Check values make sense
        assert stats.jobs_completed == 3
        assert stats.total_nodes == 100
        # 10 nodes for 50 s, 20 nodes for 50 s, and 30 nodes for 50 s.
        assert abs(sim.get_resource_area() - 3000.0) < 1e-12
        assert abs(stats.resource_area - 3000.0) < 1e-12
        assert abs(stats.utilization - (3000.0 / (100.0 * 70.0))) < 1e-12

        result.record_pass("Statistics fields and values")

    except Exception as e:
        result.record_fail("Statistics", str(e))
    finally:
        os.unlink(trace_file.name)


def test_backfill_policies(result):
    """Test 7: Different backfill policies"""
    print("\n7. Backfill Policies")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [(0, 50, 100), (10, 30, 50)])

        for policy in [dr_evt.BackfillPolicy.NONE,
                       dr_evt.BackfillPolicy.EASY,
                       dr_evt.BackfillPolicy.CONSERVATIVE]:
            params = dr_evt.SimParams()
            params.infile = trace_file.name
            params.total_nodes = 100
            params.trace_format = "simple"
            params.timestamp_format = "epoch"
            params.run_time_mode = dr_evt.RunTimeMode.LIMIT
            params.backfill_policy = policy

            sim = dr_evt.Simulation(params)
            sim.append_job(0.0, 50, QUEUE_INPUT, 100)
            sim.advance_to(0.0)
            sim.append_job(10.0, 30, QUEUE_INPUT, 50)
            sim.advance_to(200.0)

            stats = sim.get_statistics()
            assert stats.jobs_completed == 2

        result.record_pass("NONE, EASY, CONSERVATIVE policies")

    except Exception as e:
        result.record_fail("Backfill policies", str(e))
    finally:
        os.unlink(trace_file.name)


def test_priority_policies(result):
    """Test 8: Different priority policies"""
    print("\n8. Priority Policies")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [
            (0, 10, 100),   # Long job
            (5, 10, 20),    # Short job
            (10, 10, 50),   # Medium job
        ])

        for policy in [dr_evt.PriorityPolicy.FCFS,
                       dr_evt.PriorityPolicy.SJF,
                       dr_evt.PriorityPolicy.LJF]:
            params = dr_evt.SimParams()
            params.infile = trace_file.name
            params.total_nodes = 100
            params.trace_format = "simple"
            params.timestamp_format = "epoch"
            params.run_time_mode = dr_evt.RunTimeMode.LIMIT
            params.priority_policy = policy

            sim = dr_evt.Simulation(params)
            sim.append_job(0.0, 10, QUEUE_INPUT, 100)
            sim.advance_to(0.0)
            sim.append_job(5.0, 10, QUEUE_INPUT, 20)
            sim.advance_to(5.0)
            sim.append_job(10.0, 10, QUEUE_INPUT, 50)
            sim.advance_to(200.0)

            stats = sim.get_statistics()
            assert stats.jobs_completed == 3

        result.record_pass("FCFS, SJF, LJF policies")

    except Exception as e:
        result.record_fail("Priority policies", str(e))
    finally:
        os.unlink(trace_file.name)


def test_batch_mode(result):
    """Test 9: Batch mode API"""
    print("\n9. Batch Mode")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False)
    trace_file.close()

    try:
        create_test_trace(trace_file.name, [(0, 10, 50), (10, 20, 50)])

        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.LIMIT

        sim = dr_evt.Simulation(params)
        sim.initialize_trace()

        # Run entire simulation at once
        sim.run()

        stats = sim.get_statistics()
        assert stats.jobs_completed == 2
        assert stats.nodes_in_use == 0, \
            f"All jobs completed but nodes_in_use={stats.nodes_in_use}, expected 0"

        result.record_pass("Batch mode run()")

    except Exception as e:
        result.record_fail("Batch mode", str(e))
    finally:
        os.unlink(trace_file.name)


def test_checkpoint_restart(result):
    """Save and restore running, waiting, and future streaming work."""
    print("\n10. Checkpoint/Restart")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv',
                                             delete=False)
    checkpoint_file = tempfile.NamedTemporaryFile(suffix='.ckpt', delete=False)
    trace_file.close()
    checkpoint_file.close()
    try:
        create_test_trace(trace_file.name, [])
        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 100
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.LIMIT

        source = dr_evt.Simulation(params)
        source.initialize_trace()
        source.append_job(0.0, 100, QUEUE_INPUT, 50.0)
        source.append_job(0.0, 50, QUEUE_INPUT, 10.0)
        source.append_job(25.0, 25, QUEUE_INPUT, 5.0)
        source.advance_to(20.0)
        source.save_checkpoint(checkpoint_file.name)

        resumed = dr_evt.Simulation(params)
        resumed.load_checkpoint(checkpoint_file.name)
        restored = resumed.get_statistics()
        assert restored.current_time == 20.0
        assert restored.jobs_running == 1
        assert restored.jobs_waiting == 1

        source.advance_to(float("inf"))
        resumed.advance_to(float("inf"))
        expected = source.get_statistics()
        actual = resumed.get_statistics()
        assert actual.jobs_submitted == expected.jobs_submitted
        assert actual.jobs_completed == expected.jobs_completed
        assert actual.current_time == expected.current_time
        assert actual.avg_wait_time == expected.avg_wait_time

        params.backfill_policy = dr_evt.BackfillPolicy.EASY
        params.num_max_candidates = 4
        costed_jobs = []

        def compute_cost(job_id, _submit_time, _runtime, _nodes):
            costed_jobs.append(job_id)
            return job_id

        def select_last(candidates):
            return candidates[-1][0] if candidates else None

        custom_source = dr_evt.Simulation(params, compute_cost, select_last)
        custom_source.initialize_trace()
        custom_source.append_job(0.0, 100, QUEUE_INPUT, 50.0)
        custom_source.append_job(0.0, 50, QUEUE_INPUT, 10.0)
        custom_source.append_job(25.0, 25, QUEUE_INPUT, 5.0)
        custom_source.advance_to(20.0)
        custom_source.save_checkpoint(checkpoint_file.name)

        custom_resumed = dr_evt.Simulation(params, compute_cost, select_last)
        calls_before_load = len(costed_jobs)
        custom_resumed.load_checkpoint(checkpoint_file.name)
        assert len(costed_jobs) == calls_before_load
        custom_source.advance_to(float("inf"))
        custom_resumed.advance_to(float("inf"))
        assert (custom_resumed.get_statistics().avg_wait_time ==
                custom_source.get_statistics().avg_wait_time)
        result.record_pass("Ser20 checkpoint/restart")
    except Exception as e:
        result.record_fail("Checkpoint/restart", str(e))
    finally:
        os.unlink(trace_file.name)
        os.unlink(checkpoint_file.name)


def test_warm_start_batch_mode(result):
    """Python run() exposes replay-based warm-start classification/accounting."""
    print("\n10. Warm-start Batch Mode")

    trace_file = tempfile.NamedTemporaryFile(mode='w', suffix='.csv',
                                             delete=False)
    trace_file.close()
    try:
        create_replay_trace(trace_file.name)
        params = dr_evt.SimParams()
        params.infile = trace_file.name
        params.total_nodes = 10
        params.sim_start_time = 10.0
        params.trace_format = "simple"
        params.timestamp_format = "epoch"
        params.run_time_mode = dr_evt.RunTimeMode.ACTUAL

        sim = dr_evt.Simulation(params)
        sim.run()
        stats = sim.get_statistics()

        # Only the boundary/future workload is counted. Completed history,
        # the exact-boundary departure, active seeds, and inherited waiting
        # work are all absent from job accounting.
        assert stats.jobs_submitted == 2
        assert stats.jobs_completed == 2
        assert stats.jobs_running == 0
        assert stats.jobs_waiting == 0
        assert stats.total_nodes == 10
        assert stats.nodes_in_use == 0
        assert stats.nodes_available == 10
        assert abs(stats.resource_area - 51.0) < 1e-12
        assert abs(stats.utilization - 0.85) < 1e-12
        assert abs(stats.avg_wait_time - 1.5) < 1e-12
        assert abs(stats.avg_run_time - 3.0) < 1e-12
        assert abs(stats.avg_turnaround_time - 4.5) < 1e-12
        assert abs(stats.avg_bounded_slowdown - 1.0) < 1e-12
        assert abs(stats.makespan - 16.0) < 1e-12
        result.record_pass("Native replay warm start")
    except Exception as e:
        result.record_fail("Warm-start batch mode", str(e))
    finally:
        os.unlink(trace_file.name)


def main():
    print("="*60)
    print("DR_EVT Python API Test Suite")
    print("="*60)

    result = TestResult()

    # Run all tests
    test_module_import(result)
    test_streaming_example_from_repo_root(result)
    test_enumerations(result)
    test_sim_params(result)
    test_streaming_api(result)
    test_monitoring_api(result)
    test_backfill_window_api(result)
    test_custom_backfill_api(result)
    test_statistics(result)
    test_backfill_policies(result)
    test_priority_policies(result)
    test_batch_mode(result)
    if hasattr(dr_evt.Simulation, "save_checkpoint"):
        test_checkpoint_restart(result)
    test_warm_start_batch_mode(result)

    # Print summary and exit
    return result.summary()


if __name__ == '__main__':
    sys.exit(main())
