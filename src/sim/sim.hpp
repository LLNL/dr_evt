/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_SIM_SIM_HPP
#define DR_EVT_SIM_SIM_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#include <algorithm>
#include <cmath>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <memory> // unique_ptr
#include <optional>
#include <random>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "params/sim_params.hpp"
#include "sim/capacity_schedule.hpp"
#include "sim/scheduler_base.hpp"
#include "sim/scheduler_fcfs_custom.hpp"
#include "trace/dr_event.hpp"
#include "trace/trace.hpp"
#include "utils/rngen.hpp"

namespace dr_evt {

// Forward declarations
template <size_t BlockSize> class BlockWaitQueue;
class SchedulerBase;

/** \addtogroup dr_evt_sim
 *  @{ */

/**
 * Main simulation class that orchestrates job scheduling simulation
 * with backfilling using discrete event simulation
 */
template <typename TraceType> class BasicSimulation {
protected:
  /// Simulation parameters. Stored by value so the simulation owns its
  /// configuration and the caller's Sim_Params may be a temporary or go out
  /// of scope after construction.
  Sim_Params m_params;

  /// Job trace data
  TraceType m_trace;

  /// Job scheduler (polymorphic - FCFS/SJF/LJF)
  std::unique_ptr<SchedulerBase> m_scheduler;

  /// Non-owning pointer set only by the Custom-FCFS constructor.
  CustomFCFSScheduler *m_custom_scheduler;

  /// Event queue (submit, start, end events)
  event_q_t m_event_queue;

  /// Current simulation time
  sim_time_t m_current_time;

  /// Fixed physical limit used only to reject jobs that can never fit.
  num_nodes_t m_job_rejection_capacity;

  /// Ordered external machine-capacity changes and the active cursor.
  std::vector<Capacity_Change> m_capacity_changes;
  size_t m_next_capacity_change;
  num_nodes_t m_current_capacity;
  /// Integral of effective capacity over the current accounting horizon.
  /// Without a schedule, effective capacity remains m_params.m_total_nodes.
  tdiff_t m_capacity_area;
  /// Last timestamp incorporated into m_capacity_area.
  sim_time_t m_capacity_area_time;

  /// Counters
  num_jobs_t m_jobs_completed;
  num_jobs_t m_jobs_submitted; ///< Jobs accepted into the wait queue.
  num_jobs_t m_jobs_started;   ///< Jobs that began execution.
  /// Latest submit time accepted so far. The wait queues release
  /// arrivals in insertion order, so a later append with an earlier
  /// submit time would be held until this time; such appends are
  /// rejected instead.
  sim_time_t m_latest_submit_time;
  /// Next progressive input file; advanced once its jobs are fully admitted.
  size_t m_next_progressive_file;
  /// True after load_checkpoint() restores runnable state.
  bool m_checkpoint_loaded;
  /// Completion count written by the last automatic checkpoint.
  num_jobs_t m_last_automatic_checkpoint_jobs;
  /// Whether this run has written an automatic checkpoint.
  bool m_has_automatic_checkpoint;
  /// Simulation time written by the last automatic checkpoint.
  sim_time_t m_last_automatic_checkpoint_time;
  /// Historical jobs still running during the temporary warm-start stage.
  size_t m_pre_start_jobs;
  /// Post-boundary node-seconds contributed by suppressed historical jobs.
  tdiff_t m_warm_resource_area;
  /// Latest departure among historical jobs active after the boundary.
  sim_time_t m_warm_resource_end;

  /// Serializable random-number engine used for duration sampling.
  RNGen<> m_rng;

  // NOTE: Wait queue now owned by scheduler (m_scheduler maintains internal
  // queue)

  /// Running jobs for streaming mode (job_idx -> start_time)
  running_jobs_t m_running_jobs;

  // Output statistics only; the scheduler never consults these values.
  /// Sum of the waiting-job counts observed by arriving jobs.
  size_t m_queue_length_sum;
  size_t m_queue_length_samples; ///< Number of arriving-job observations.
  size_t m_queue_length_peak;    ///< Largest observed waiting-queue length.

  /// Accepted arrivals not yet observed at their simulation timestamps.
  std::map<sim_time_t, size_t> m_pending_queue_arrivals;

  // Compact, permanent query records for jobs created by append_job(s).
  // Trace records may be reclaimed after completion, so their scheduling
  // timestamps cannot by themselves satisfy the public status API.
  struct Appended_Job_Query_Record {
    sim_time_t submit_time = 0.0;
    tdiff_t limit_time = 0.0;
    num_nodes_t num_nodes = 0;
    sim_time_t start_time = 0.0;
    sim_time_t end_time = 0.0;
    bool tracked = false;
    bool scheduled = false;
    bool rejected = false;
  };
  job_no_t m_first_appended_job_idx = std::numeric_limits<job_no_t>::max();
  std::vector<Appended_Job_Query_Record> m_appended_job_query_records;

public:
  /**
   * @brief Construct a simulation from immutable configuration.
   * @param[in] params Immutable simulation configuration.
   */
  BasicSimulation(Sim_Params params);

  /**
   * @brief Construct a simulation with experimental external backfill choice.
   * @param[in] params Immutable simulation configuration.
   * @param[in] cost_function Callback that computes a job's cost at insertion.
   * @param[in] selector Callback that chooses one of the reported candidates.
   */
  BasicSimulation(Sim_Params params, job_cost_function_t cost_function,
                  backfill_selector_t selector);

  /** Construct a simulation with a configured Custom-FCFS subclass. */
  BasicSimulation(Sim_Params params,
                  std::unique_ptr<CustomFCFSScheduler> scheduler);

  /**
   * @brief Run a complete batch simulation for the configured trace.
   * @details
   * Loads and prepares the input trace when necessary and submits its jobs.
   * With max_time configured, processes events through that inclusive
   * boundary; otherwise drains the event queue. For externally fed work, use
   * append_job() or append_jobs() followed by advance_to() instead.
   */
  void run();

  /**
   * @brief Write a human-readable statistics report.
   * @param[in,out] os Destination stream receiving the report.
   */
  void print_stats(std::ostream &os) const;

  /**
   * @brief Write the simulated job trace to the configured output file.
   * @details
   * The destination is Sim_Params::get_outfile(). This does not advance
   * simulation time or otherwise change scheduling state.
   */
  void write_simulated_trace();

  /**
   * @brief Write and reclaim the completed front prefix at current time.
   * @details This is an explicit streaming checkpoint. It does not close the
   * schedule output, and it never reclaims an unfinished or pending record.
   */
  void flush_completed_jobs();

#if defined(DR_EVT_HAS_SER20)
  /**
   * @brief Save an exact continuation checkpoint to a binary stream.
   * @details Standard and Pcon traces are supported. Callback-based Custom FCFS
   * is supported when restart constructs the destination with equivalent
   * callbacks; callback objects themselves are not serialized. The checkpoint
   * is intended for the same DR_EVT build and matching Sim_Params. Buffered
   * output is flushed before the snapshot is written. Output files are not
   * included in the checkpoint; their committed byte boundaries are.
   * @param[in,out] output Binary destination stream.
   */
  void save_checkpoint(std::ostream &output);

  /**
   * @brief Save an exact continuation checkpoint to a file.
   * @param[in] filename Destination path, replaced after state is serialized.
   */
  void save_checkpoint(const std::string &filename);

  /**
   * @brief Restore a checkpoint into this simulation.
   * @details This replaces any current state. Configuration, scheduler kind,
   * and output paths must match those used to create the checkpoint. A Custom
   * FCFS destination must be constructed with equivalent callbacks. Existing
   * file outputs are archived as pre-restart segments, and fresh output
   * segments are opened at the configured paths.
   * @param[in,out] input Binary checkpoint source stream.
   */
  void load_checkpoint(std::istream &input);

  /**
   * @brief Restore an exact continuation checkpoint from a file.
   * @param[in] filename Existing checkpoint path.
   */
  void load_checkpoint(const std::string &filename);
#endif

  /**
   * @brief Write resource-allocation history to a CSV file.
   * @param[in] filename Destination CSV path.
   */
  void write_resource_trace(const std::string &filename);

  /**
   * One job's data for append_jobs() - the same fields append_job()
   * takes individually (submit_time here using the same external,
   * pre-epoch-conversion sim_time_t append_job() takes, not Trace's
   * internal epoch_t), grouped so several new jobs can be passed in a
   * single call.
   */
  struct Job_Append_Request {
    sim_time_t submit_time; ///< Requested arrival time.
    num_nodes_t num_nodes;  ///< Requested node count.
    std::string queue;      ///< Queue ID, or a name in legacy-input builds.
    tdiff_t limit_time;     ///< Requested time limit.
    std::optional<tdiff_t> actual_run_time =
        std::nullopt; ///< Known execution time, if any.
  };

  /**
   * @brief Add a new job record and enqueue it for scheduling.
   *
   * Append a genuinely new job - the streaming counterpart to
   * load_data(), for a job the trace has never seen before (as
   * opposed to the internal submit_job() helper, which enqueues a job
   * already present in m_data). Adds the job to the store and immediately
   * enqueues it for scheduling.
   *
   * @param[in] submit_time Arrival time, which must be >= current time and
   *        >= the submit time of every job appended before it.
   * @param[in] num_nodes Number of nodes the job requests.
   * @param[in] queue Numeric queue ID by default (for example, "1" for the
   * default Queue1), or a queue name such as "pbatch" when built with
   * `DR_EVT_LEGACY_QUEUE_INPUT`.
   * @param[in] limit_time User-estimated time limit in seconds.
   * @param[in] actual_run_time Known execution time, or empty to use
   *        limit_time as the streaming execution duration.
   * @return New Trace job identifier as job_no_t.
   * @throws std::invalid_argument if limit_time is not a positive,
   *         representable whole number of seconds, if actual_run_time is
   *         non-positive, non-finite, or greater than limit_time, or if
   *         submit_time is earlier than an already appended job's.
   * @see submit_job()
   * @see SchedulerBase::insert_job()
   */
  job_no_t append_job(sim_time_t submit_time, num_nodes_t num_nodes,
                      const std::string &queue, tdiff_t limit_time,
                      std::optional<tdiff_t> actual_run_time = std::nullopt);

  /**
   * @brief Add several new job records and enqueue all of them.
   *
   * Append several genuinely new jobs in one call - the batch
   * counterpart to append_job(), for the same never-seen-before case.
   * Validates every request's submit_time >= current_time before any
   * of them are appended (same precondition append_job() enforces per
   * job) - combined with Trace::append_jobs()'s own validation
   * (ordering, capacity for the whole batch), this call is fully
   * all-or-nothing: nothing is appended unless the whole batch can be.
   * See Trace::append_jobs()'s own doc comment for the rest.
   *
   * @param[in] requests New jobs' data in submit_time order (see
   *        Trace::append_jobs() for why - this function forwards
   *        requests as-is, so pass them there already sorted).
   * @return New Trace job identifiers in the same order as requests.
   * @throws std::invalid_argument if any limit_time is not a positive,
   *         representable whole number of seconds, or if an actual_run_time
   *         is invalid or exceeds its limit_time.
   * @see append_job()
   * @see submit_job()
   */
  std::vector<job_no_t>
  append_jobs(const std::vector<Job_Append_Request> &requests);

  /** Lifecycle state returned by get_job_statuses(). */
  enum class Job_State { PENDING, RUNNING, COMPLETED, REJECTED };

  /** Point-in-time status of one job created by append_job(s). */
  struct Job_Status {
    job_no_t job_idx;
    Job_State state;
    std::optional<sim_time_t> start_time;
    std::optional<sim_time_t> end_time;
    std::optional<sim_time_t> expected_start_time;
  };

  /**
   * @brief Query appended jobs by their stable identifiers.
   * @details Pending jobs receive an on-demand projected start based on the
   * scheduler's current queue order, running-job limit-time releases, and
   * current capacity. Scheduled jobs return their actual simulated start and
   * end timestamps. Results preserve request order and duplicate IDs.
   * @throws std::out_of_range if an ID was not returned by append_job(s).
   */
  std::vector<Job_Status>
  get_job_statuses(const std::vector<job_no_t> &job_idxs) const;

protected:
  /**
   * @brief Validate an existing job and enqueue it in the scheduler.
   *
   * Submit an already-known job to the scheduler's waiting queue.
   * This is an implementation detail used by append_job(), append_jobs(),
   * and batch loading; external streaming callers use append_job().
   * The internal scheduler will decide when to start the job based on
   * resources and backfilling policy.
   *
   * @param[in] job_idx Existing Trace job identifier to submit.
   * @param[in] submit_time Arrival time, which must be >= current time.
   *
   * This does not add a job record to Trace: job_idx must already identify
   * one. It validates the arrival, records arrival-time accounting, then
   * calls SchedulerBase::insert_job() to enqueue the scheduling data.
   * Call advance_to() to let the scheduler select a job to start; that
   * path calls Trace::insert_job() to record the start and end events.
   *
   * @see append_job()
   * @see SchedulerBase::insert_job()
   * @see Trace::insert_job()
   */
  void submit_job(job_no_t job_idx, sim_time_t submit_time);

  void record_appended_job(job_no_t job_idx, sim_time_t submit_time,
                           num_nodes_t num_nodes, tdiff_t limit_time);
  void record_appended_job_start(job_no_t job_idx);

public:
  /**
   * @brief Advance simulation time and start eligible queued jobs.
   *
   * Advance simulation to target time (streaming mode)
   * Processes all events up to target_time and lets the scheduler make
   * decisions about which jobs to start.
   *
   * @param[in] target_time Time to advance to, which must be >= current time.
   *
   * PRECONDITION: Caller guarantees no jobs will be submitted with
   * submit_time < target_time. This means either:
   * - All jobs have already been submitted, OR
   * - External tool knows the next job arrival is at >= target_time
   *
   * POSTCONDITION: for a finite target, m_current_time == target_time and all
   * scheduling decisions have been made up to that time. The maximum
   * representable value is treated as a drain sentinel; after draining,
   * m_current_time is the last real event time.
   *
   * Jobs selected by the scheduler are recorded through Trace::insert_job().
   * @see submit_job()
   * @see Trace::insert_job()
   */
  void advance_to(sim_time_t target_time);

  /**
   * @brief Get the number of nodes currently in use.
   * @return Allocated-node count as num_nodes_t.
   */
  num_nodes_t get_nodes_in_use() const;

  /**
   * @brief Return instantaneous node utilization.
   * @return Nodes currently used divided by effective current capacity.
   * @details Effective capacity is the scheduled capacity, raised to current
   * occupancy while a non-preemptive reduction is still draining. The result
   * therefore remains in [0,1], including when scheduled capacity is zero.
   */
  double get_current_utilization() const {
    const auto used = get_nodes_in_use();
    const auto effective_capacity = std::max(m_current_capacity, used);
    return effective_capacity == 0
               ? 0.0
               : static_cast<double>(used) /
                     static_cast<double>(effective_capacity);
  }

  /**
   * @brief Return Custom-FCFS time-accounted resource usage in node-seconds.
   * @details Available only when this simulation was constructed with the
   * CustomFCFSScheduler callback constructor. Accounting occurs once per
   * settled scheduling timestamp and includes the elapsed portion of the
   * current interval for finite snapshots.
   * @return Resource area in node-seconds.
   * @throws std::logic_error if this simulation does not use Custom FCFS.
   */
  tdiff_t get_resource_area() const;

  /**
   * @brief Get the current simulation time.
   * @return Current simulation time as sim_time_t.
   */
  sim_time_t get_current_time() const { return m_current_time; }

  /**
   * @brief Get mutable trace data for streaming-mode access.
   * @return Mutable reference to the simulation Trace.
   */
  TraceType &get_trace() { return m_trace; }
  /** @brief Return read-only access to the simulation trace.
   * @return Read-only reference to the simulation Trace. */
  const TraceType &get_trace() const { return m_trace; }

  // ========================================================================
  // Monitoring and Statistics API for Python/External Tools
  // ========================================================================

  /**
   * @brief Get the number of available nodes.
   * @return Unallocated-node count as num_nodes_t.
   */
  num_nodes_t get_available_nodes() const {
    const auto used = get_nodes_in_use();
    return used < m_current_capacity ? m_current_capacity - used : 0;
  }

  /** @brief Capacity currently available to this simulated workload. */
  num_nodes_t get_current_capacity() const { return m_current_capacity; }

  /**
   * @brief Get the count of jobs that have arrived but remain unscheduled.
   *
   * Uses SchedulerBase::active_job_count(), which is correct for all four
   * scheduler implementations - see SchedulerBase::sync_to() for the
   * full explanation of how eligibility tracking stays fresh without
   * this method needing to pass or check a time itself. Safe to call
   * with no time argument because nothing can call this while
   * advance_to() is mid-execution (single-threaded, no reentrancy) -
   * by the time this runs, the scheduler is already synced to
   * m_current_time from the last completed advance_to() call (or from
   * construction, if none has run yet).
   *
   * @return Waiting-job count as size_t.
   */
  size_t get_active_job_count() const {
    return m_scheduler->active_job_count();
  }

  /**
   * @brief Get the estimated start time of the FCFS queue head.
   * Returns the shadow time (earliest time head of queue can start)
   * @return Estimated start time as sim_time_t, or -1 when the queue is empty.
   */
  sim_time_t get_fcfs_head_shadow_time() const {
    if (m_scheduler->active_job_count() == 0) {
      return -1.0;
    }
    return m_scheduler->get_fcfs_reservation_time();
  }

  /**
   * @brief Point-in-time FCFS/EASY reservation projection.
   *
   * @details Returned by get_backfill_window() for a caller evaluating
   * whether a candidate can backfill without delaying the FCFS queue head.
   * `current_time` and `available_nodes` describe capacity immediately;
   * `releases` then describes every projected capacity increase from the
   * currently running jobs. Release events use the time-limit estimates used
   * by SchedulerBase::calculate_fcfs_reservation, rather than actual runtimes,
   * so the projection and reservation agree. `shadow_time` is -1 when no
   * FCFS head is waiting.
   */
  struct Backfill_Window {
    /**
     * @brief One future capacity increase in a Backfill_Window.
     *
     * @details Entries in Backfill_Window::releases are sorted by time.
     * Multiple job completions at the same time are combined into one
     * record whose nodes_released is their total.
     */
    struct Resource_Release {
      sim_time_t time;            ///< Absolute simulation time of the release.
      num_nodes_t nodes_released; ///< Nodes becoming available at time.
    };

    sim_time_t current_time;     ///< Time at which this snapshot was captured.
    num_nodes_t available_nodes; ///< Nodes free immediately at current_time.
    sim_time_t
        shadow_time; ///< Reserved FCFS-head start time, or -1 if no head waits.
    std::vector<Resource_Release> releases; ///< Projected capacity increases.
  };

  /**
   * @brief Return the current EASY-backfilling reservation projection.
   * @details
   * The result is a snapshot: available nodes and release times reflect
   * current scheduler state, while release times are based on the same
   * time-limit estimates used for the FCFS reservation. Releases include
   * every currently running job, including when no FCFS head waits.
   * @return Backfill_Window value for the current simulation time.
   */
  Backfill_Window get_backfill_window() const;

  /**
   * @brief Estimate the Custom-FCFS waiting-queue drain time.
   * @details Computes the resource-area prediction horizon beginning at the
   * FCFS head's shadow time. Current waiting demand A_Q is the sum of each
   * waiting job's requested nodes times estimated runtime. Service capacity
   * is projected from current running-job releases and scaled by utilization.
   * Future arrivals are not included.
   *
   * The horizon is estimated only after the current FCFS-EASY backfilling
   * cycle has completed. Consequently, all jobs that can be immediately
   * dispatched have already entered the running set, and the waiting queue
   * contains only jobs that remain waiting. The running set includes jobs
   * newly dispatched during the cycle. The estimator then forward-replays
   * predicted completions of that fixed running-job set without admitting
   * additional jobs from the waiting queue.
   *
   * Between completion events, available resources are constant. If the
   * accumulated usable area reaches queued demand within one such interval,
   * that interval's ending completion event defines the horizon. If the last
   * currently running job completes first, the remaining demand is estimated
   * continuously using utilization times total system capacity. Utilization
   * is an expected capacity factor for fragmentation and scheduling losses;
   * it is not instantaneous utilization at the current scheduling time. An
   * input value of zero selects the fallback factor U = 1.
   *
   * @pre The caller has completed scheduling at the current time, normally by
   * calling advance_to(get_current_time()) after adding jobs at that time.
   * @pre The selected scheduler supports on-demand waiting-resource-area
   * queries and uses EASY backfilling.
   * @param[in] utilization Expected system-utilization factor in (0, 1], or
   * zero to use the fallback factor 1.
   * @return Horizon duration from the shadow time. Returns zero for an empty
   * queue and infinity only if positive demand exists with zero total system
   * resources.
   * @throws std::invalid_argument if utilization is non-finite, negative, or
   * greater than 1.
   * @throws std::logic_error if the scheduler does not support prediction or
   * does not use EASY backfilling.
   */
  tdiff_t get_prediction_horizon(double utilization) const;

  /**
   * Get detailed scheduling statistics
   * @return Structure with wait times, turnaround, utilization
   */
  struct Statistics {
    num_jobs_t jobs_submitted;   ///< Jobs accepted into the wait queue.
    num_jobs_t jobs_started;     ///< Jobs that have begun execution.
    num_jobs_t jobs_completed;   ///< Jobs that have completed execution.
    num_jobs_t jobs_running;     ///< Jobs executing at the snapshot time.
    num_jobs_t jobs_waiting;     ///< Jobs awaiting scheduler placement.
    sim_time_t current_time;     ///< Time represented by this snapshot.
    num_nodes_t total_nodes;     ///< Configured cluster capacity.
    num_nodes_t nodes_in_use;    ///< Nodes allocated at current_time.
    num_nodes_t nodes_available; ///< Nodes free at current_time.
    /**
     * @brief Allocated-node area in node-seconds.
     * @details Live time-accounted area for Custom FCFS; scheduled-job area
     * for standard schedulers.
     */
    tdiff_t resource_area;
    /**
     * @brief Resource-area utilization.
     * @details Divides allocated-node area by effective-capacity area. Uses
     * the live accounting horizon for Custom FCFS and makespan for standard
     * schedulers.
     */
    double utilization;
    tdiff_t avg_wait_time;       ///< Mean completed-job wait duration.
    tdiff_t avg_run_time;        ///< Mean completed-job execution duration.
    tdiff_t avg_turnaround_time; ///< Mean completed-job submit-to-end duration.
    double avg_bounded_slowdown; ///< Mean bounded slowdown (10-second bound).
    sim_time_t makespan;         ///< Latest completion time in the trace.
  };

  /**
   * @brief Calculate aggregate and point-in-time simulation statistics.
   * @return Statistics value derived from the trace and current scheduler
   * state.
   */
  Statistics get_statistics() const;

  /**
   * @brief Run until just before a target time.
   *
   * Excludes events occurring exactly at target_time.
   * @param[in] target_time Exclusive upper time bound as sim_time_t.
   */
  void run_until_exclusive(sim_time_t target_time) {
    // Process every pending event with time < target_time, leaving events
    // at exactly target_time for a later advance_to(). Pending work lives
    // in three places: the trace's start/end event queue, the scheduler's
    // future arrivals (appended jobs), and the capacity schedule. This
    // used to scan m_event_queue, which nothing populates outside of
    // checkpoint I/O, so it never advanced at all.
    if (target_time <= m_current_time) {
      return;
    }
    sim_time_t previous = -std::numeric_limits<sim_time_t>::infinity();
    while (true) {
      m_scheduler->sync_to(m_current_time); // get_next_arrival_time() needs it
      sim_time_t next = m_scheduler->get_next_arrival_time();
      if (!m_trace.pending_events().empty()) {
        const auto &ts = m_trace.pending_events().begin()->get_time();
        next = std::min(next, static_cast<sim_time_t>(ts.first) + ts.second);
      }
      if (m_next_capacity_change < m_capacity_changes.size()) {
        next = std::min(next, m_capacity_changes[m_next_capacity_change].time);
      }
      // Stop at the first event at or after the target, or if no progress
      // was made (defensive; advance_to() consumes everything <= next).
      if (next >= target_time || next <= previous) {
        break;
      }
      previous = next;
      advance_to(next);
    }
  }

  /**
   * @brief Initialize trace data and reset simulation state.
   *
   * Load trace data, sort by submission time, and
   * determine job durations (simulation mode only). Resets simulation
   * state (m_current_time, job counters) for a fresh run.
   *
   * Public streaming callers (Python bindings, gRPC server) must call
   * this instead of calling get_trace().load_data() directly - that
   * skips the sort and duration-determination steps below, which was a
   * real, previously undetected bug affecting both the existing Python
   * bindings and an early gRPC server draft: jobs loaded that way get
   * silently wrong actual-duration data, which cascades into wrong
   * scheduling decisions and wrong avg_wait_time/avg_turnaround_time/
   * makespan statistics, with no error raised anywhere.
   *
   * @param[in] max_jobs Maximum jobs to load. 0 (the default)
   *                 falls back to m_params.m_max_jobs if that was set,
   *                 or no limit otherwise - this is what run() relies on
   *                 for batch mode. Streaming callers should pass an
   *                 explicit value here instead of relying on Sim_Params.
   * @return Number of jobs loaded as num_jobs_t.
   */
  num_jobs_t initialize_trace(num_jobs_t max_jobs = 0);

protected:
  /**
   * Advance using compile-time-selected accounting and warm-start paths.
   * WarmStage is instantiated only while historical jobs remain; the normal
   * stage contains no pre-start-time condition.
   */
  template <bool AccountResources, bool WarmStage = false>
  void advance_to_impl(sim_time_t target_time,
                       CustomFCFSScheduler *custom_scheduler);

  /** Seed replay jobs active before m_params.m_sim_start_time, then reschedule
   * post-boundary work through the ordinary scheduler. */
  void run_warm_start();

  /**
   * @brief Process an arrival event for an existing trace job.
   * @param[in] job_idx Identifier of the arriving job.
   */
  void process_submit_event(job_no_t job_idx);

  /**
   * @brief Process a scheduler-selected job start event.
   * @param[in] job_idx Identifier of the job to start.
   */
  void process_start_event(job_no_t job_idx);

  /**
   * @brief Process a job completion event and release its resources.
   * @param[in] job_idx Identifier of the completing job.
   */
  void process_end_event(job_no_t job_idx);

  /**
   * @brief Process the chronologically next queued event.
   * @return true when an event was processed; false when the queue is empty.
   */
  bool advance_to_next_event();

  /**
   * @brief Queue start events for jobs selected by the scheduler.
   * @param[in] jobs Identifiers of jobs that can start at the current time.
   */
  void schedule_start_events(const std::vector<job_no_t> &jobs);

  /**
   * @brief Queue the completion event for a started job.
   * @param[in] job_idx Identifier of the running job.
   * @param[in] start_time Time at which the job started.
   */
  void schedule_end_event(job_no_t job_idx, sim_time_t start_time);

  /**
   * @brief Determine actual durations for all jobs in simulation mode.
   *
   * Called during initialization before simulation starts
   */
  void determine_job_run_time();

  /**
   * @brief Determine actual durations for selected jobs.
   *
   * Same as determine_job_run_time(), but only for the given jobs -
   * for progressive loading, where each file's jobs need this done
   * once they're loaded, not just once for the initial batch.
   * @param[in] job_nos Permanent identifiers of jobs to update.
   */
  void determine_job_run_time(const std::vector<job_no_t> &job_nos);

  /**
   * @brief Determine the simulated duration of one job.
   * @param[in,out] job Trace record whose simulated duration is updated.
   */
  void determine_one_job_run_time(Job_Record &job);

  /**
   * @brief Run the configured sequence of trace files incrementally.
   * @details
   * Loads each file in turn, submits its jobs in submit-time order, then
   * advances to its last arrival before loading the next file. A final
   * advance drains outstanding work. REPLAY input is not supported.
   */
  void run_progressive();

  /**
   * @brief Write a configured automatic checkpoint when it is due.
   * @param[in] file_boundary Whether a progressive file was fully processed.
   */
  void maybe_save_automatic_checkpoint(bool file_boundary);

  /**
   * @brief Record what each job arriving now sees ahead of it in the queue.
   * @details The scheduler must already be synchronized to current_time. For
   * several jobs with the same timestamp, input order defines their
   * observations: if n earlier jobs are waiting, they observe n, n+1, ... .
   */
  void record_queue_arrivals(sim_time_t current_time);

  /** Reset the capacity cursor to the configured maximum. */
  void reset_capacity_schedule();

  /** Reset time-integrated effective-capacity accounting at start_time. */
  void reset_capacity_accounting(sim_time_t start_time);

  /** Accumulate effective capacity through current_time. */
  void advance_capacity_accounting_to(sim_time_t current_time);

  /** Return effective-capacity area through a finite snapshot time. */
  tdiff_t capacity_area_through(sim_time_t through_time) const;

  /** Apply every capacity change effective at the supplied timestamp. */
  bool apply_capacity_changes(sim_time_t current_time);

  /**
   * @brief Sample a job duration from the configured distribution.
   * @param[in] time_limit User-provided time limit.
   * @param[in] dist Distribution type.
   * @param[in] scale Distribution scale factor.
   * @param[in] stddev Distribution standard-deviation factor.
   * @return Sampled duration as tdiff_t.
   */
  tdiff_t sample_run_time(tdiff_t time_limit, DistributionType dist,
                          double scale, double stddev);
};

using Simulation = BasicSimulation<Trace>;
using PconSimulation = BasicSimulation<PconTrace>;
using SwfSimulation = BasicSimulation<SwfTrace>;

extern template class BasicSimulation<Trace>;
extern template class BasicSimulation<PconTrace>;
extern template class BasicSimulation<SwfTrace>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_SIM_HPP
