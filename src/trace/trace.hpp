/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file trace.hpp
 * @brief Trace storage, event processing, streaming append, and output APIs.
 */

#ifndef DR_EVT_TRACE_TRACE_HPP
#define DR_EVT_TRACE_TRACE_HPP

#include <boost/circular_buffer.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "common.hpp"
#include "params/sim_params.hpp" // CircularOverflowPolicy
#include "trace/data_columns.hpp"
#include "trace/dr_event.hpp"
#include "trace/job_record.hpp"
#include "trace/trace_policy.hpp"

namespace dr_evt {
template <typename TraceType> class BasicSimulation;
#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
class RedisOutput;
#endif
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * @brief One job's data for Trace::append_jobs() - the same fields
 * Trace::append_job() takes individually, grouped so a caller can pass
 * several new jobs (never before seen by this Trace) in a single call.
 * Deliberately not a Job_Record itself: append_jobs() only needs a new
 * job's externally-supplied attributes, not Job_Record's full internal
 * state (scheduling sentinels, simulated-vs-real flags, etc.), which
 * Job_Record's own constructor already initializes correctly.
 */
struct Job_Append_Request {
  epoch_t submit_time;              ///< Arrival timestamp for the new job.
  num_nodes_t num_nodes;            ///< Requested node count.
  job_queue_t queue = QueueUnknown; ///< Queue identity, initially unknown.
  timeout_t limit_time;             ///< Requested execution-time limit.
  std::optional<tdiff_t> actual_run_time =
      std::nullopt; ///< Known execution time, if any.
};

/** @brief Job store, event queue, and output-trace state for one workload. */
template <typename Policy> class BasicTrace : private Policy {
  template <typename TraceType> friend class BasicSimulation;

public:
  /// Circular buffer, front-only reclaim: a job's slot becomes reusable
  /// once safe (see is_front_reclaimable()), same shape as
  /// m_ctx.m_resource_history but with a real safety check instead of
  /// "always safe" - a job may still be running when its slot would
  /// otherwise be reclaimed. No compaction, no removed flag:
  /// out-of-order completions (backfilling) just sit in place until
  /// every job ahead of them (by job_no) has also been reclaimed - see
  /// OUTPUT_TRACE_BUFFERS.md.
  ///
  /// Reclaiming a slot makes it reusable within this buffer's fixed,
  /// already-allocated capacity - it does not free memory back to the
  /// OS. In batch mode (the only mode today - see
  /// OUTPUT_TRACE_BUFFERS.md), load_data() sizes capacity to the whole
  /// trace before the run starts, so this essentially fires at most
  /// once per run: size() only shrinks afterward (nothing new is ever
  /// inserted once loading finishes), so the buffer stops being full()
  /// after the very first reclaim.
  /// Reclaimable circular store of loaded and streamed job records.
  using policy_type = Policy;
  using trace_data_t = typename Policy::job_store_type;
  using resource_sample_t = typename Policy::resource_sample_type;
  /// Time intervals reserved for non-batch work when enabled.
  using reserved_t = std::vector<period_t>;

protected:
  const std::string m_fname; ///< Name of the input datafile
  Data_Columns m_dcols;      ///< Header info and column filter
  trace_data_t m_data;       ///< Job trace data

  /// job_no of the oldest job still physically present in m_data - the
  /// whole translation layer between the permanent, ever-increasing
  /// job_no identifiers callers use and m_data's own physical slots:
  /// job_at(job_no) is just m_data[job_no - m_num_reclaimed]. Valid only
  /// because reclaiming is front-only with no compaction - if it ever
  /// removed from the middle, this single global offset couldn't
  /// describe the shift (jobs before vs. after the removed one would
  /// need different corrections). Advances only via reclaim_front_jobs().
  size_t m_num_reclaimed;
  /// Requested job-store capacity; zero selects the loaded-trace size.
  size_t m_job_store_capacity;
  /// True after the requested job-store capacity has been resolved.
  bool m_job_store_capacity_resolved;
  CircularOverflowPolicy
      m_job_store_overflow; ///< Fallback when the front isn't safe to reclaim
                            ///< and the buffer's full
  /// Number of processed departures between opportunistic completed-prefix
  /// flushes. Zero follows the current job-store capacity.
  size_t m_job_flush_interval;
  /// Departures processed since the last capacity, explicit, or interval
  /// flush. This is trace-level state, not a per-job flag.
  size_t m_departures_since_job_flush;
  /// Permanent half-open replay job range whose start/end events have been
  /// inserted. This prevents an end_time alone from making a not-yet-enqueued
  /// replay record appear committed.
  size_t m_replay_jobs_enqueued;
  double m_memory_pressure_fraction =
      0.0; ///< 0.0 = disabled; see set_memory_pressure_fraction()

  /// Tracks continuity across load_next_file() calls: the latest
  /// submit_time seen across every file loaded so far this way, so
  /// the next file's earliest submit_time can be checked against it
  /// (>=). m_has_loaded_a_file distinguishes "no previous file yet"
  /// (first call, nothing to check against) from a real value -
  /// simpler and clearer than reserving a sentinel epoch_t for the
  /// same purpose.
  /// Whether progressive loading has established a preceding file boundary.
  bool m_has_loaded_a_file = false;
  /// Last submit time from the preceding progressively loaded file.
  epoch_t m_last_loaded_submit_time;

  /// Running totals over every is_scheduled() job written via
  /// write_job_line() - accumulated there (the single choke point for
  /// both reclaim-time and final-flush writes) so a reclaimed job's
  /// contribution isn't lost the way iterating m_data after the fact
  /// would miss it. Sum+count, not the full per-job list - trivial to
  /// maintain incrementally, same idea as record_resource_sample().
  /// Number of scheduled jobs emitted to the simulated trace.
  num_jobs_t m_completed_count;
  /// Accumulated wait time of emitted scheduled jobs.
  tdiff_t m_wait_time_sum;
  /// Accumulated turnaround time of emitted scheduled jobs.
  tdiff_t m_turnaround_time_sum;
  /// Latest completion time among emitted scheduled jobs.
  sim_time_t m_makespan;
#if MARK_DAT_PERIOD
  /// Period where resources were unavailable for batch jobs
  reserved_t m_reserved;
#endif

  /// Timezone metadata for human-readable output
  std::string
      m_default_timezone; ///< Default timezone offset (e.g., "+00:00" for UTC)
  std::map<std::string, std::string>
      m_queue_timezones; ///< Per-queue timezone overrides

public:
  /// Tracing context, i.e., temporary data while running simulation -
  /// now owned by Trace itself (m_ctx below), not supplied externally.
  struct Context {
#if MARK_DAT_PERIOD
    num_jobs_t m_pAll_cnt; // On-going pAll job
    epoch_t m_dat_start;
    epoch_t m_dat_end;
    tdiff_t m_dat_span;
    job_queue_t m_prev_job_q;
#endif
    num_nodes_t m_n_nodes_in_use; ///< Nodes allocated by processed events.
    event_q_t m_evtq;             ///< Pending resource start and end events.

    /// (time, allocated_nodes) sampled every time an event changes
    /// occupancy. free_nodes isn't stored here since total_nodes isn't
    /// known to Context - it's derived by write_resource_trace() below.
    /// A circular buffer bounds memory for long-running/streaming
    /// sessions: every entry here is immediately safe to reclaim (this
    /// is a strictly time-ordered append log, unlike the job-record
    /// store, which must wait for a job's end_time to pass) so, unlike
    /// a wait queue or job store, reclaiming here never needs an
    /// abort/grow fallback - it always succeeds. Capacity is resolved
    /// lazily (see Trace::start_resource_trace()) since the natural
    /// default depends on the job trace's size, known only once
    /// loaded, not at Context's own construction time.
    boost::circular_buffer<resource_sample_t> m_resource_history;

    /** @brief Construct an empty trace-processing context. */
    Context();
    /** @brief Render the transient simulation context for diagnostics. */
    std::string to_string() const;
  };

protected:
  /// This Trace's own simulation context - owned here so that a Trace
  /// handed to any caller (Simulation, or a future streaming driver)
  /// always carries its own state together, rather than a caller having
  /// to keep a separate Context in sync with the right Trace.
  Context m_ctx;

  /// Requested capacity for m_ctx.m_resource_history (0 = auto-size to
  /// the job trace once loaded - see resolve_resource_history_capacity()).
  /// Set via set_resource_history_capacity()/start_resource_trace(),
  /// resolved lazily the first time a sample is recorded.
  size_t m_resource_history_capacity;
  /// True after the resource-history capacity has been resolved.
  bool m_resource_history_capacity_resolved;

  /// Open incremental resource-trace file stream when Redis is not selected.
  std::ofstream m_resource_trace_ofs;
  /// Cluster size used to derive free nodes in resource-trace output.
  num_nodes_t m_resource_trace_total_nodes;
  /// Capacity effective at the current resource-history timestamp.
  num_nodes_t m_resource_trace_current_capacity;
  /// Whether the initial resource capacity has been installed.
  bool m_resource_capacity_initialized;
  /// Timestamp used for the first row when resource recording begins.
  sim_time_t m_resource_recording_start;
  /// Policy-aware snapshot retained when warm output is opened after the run.
  resource_sample_t m_resource_recording_baseline;
  /// Whether m_resource_recording_baseline should replace the default row.
  bool m_has_resource_recording_baseline;
  /// Whether resource-trace timestamps are rendered in milliseconds.
  bool m_resource_trace_msec;

  /// Set once start_simulated_trace() opens a real output file - lets
  /// reclaim_front_jobs() write each job's line at reclaim time, same
  /// reasoning as m_resource_trace_ofs above: a job's slot may be
  /// reused before write_simulated_trace()'s old one-pass-at-the-end
  /// write would ever see it.
  /// Open incremental simulated-job-trace output stream, when enabled.
  std::ofstream m_simulated_trace_ofs;
#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
  /// Redis schedule sink, mutually exclusive with m_simulated_trace_ofs.
  std::unique_ptr<RedisOutput> m_redis_output;
#endif
  /// Whether simulated-job-trace timestamps are rendered in milliseconds.
  bool m_simulated_trace_msec;
  /// Permanent number of the next job whose schedule output/statistics have
  /// not yet been consumed. This is the output cursor across front reclamation,
  /// explicit flushes, and the final write.
  size_t m_next_job_to_write;
  /// Accumulates complete file-output lines during one record-count flush.
  std::string m_simulated_trace_buffer;

public:
  /** @brief Construct a trace using the default input format. @param[in] fname
   * Input path. */
  BasicTrace(const std::string &fname);
  /** @brief Construct a trace with an explicit input format.
   * @param[in] fname Input path. @param[in] format Trace format name. */
  BasicTrace(const std::string &fname, const std::string &format);
  /** @brief Construct a trace with explicit input and timestamp formats.
   * @param[in] fname Input path.
   * @param[in] format Trace format name.
   * @param[in] timestamp_format Retained epoch/iso compatibility setting;
   * input timestamp encoding is auto-detected.
   * @param[in] timezone Default timezone for timestamps without offsets. */
  BasicTrace(const std::string &fname, const std::string &format,
             const std::string &timestamp_format, const std::string &timezone);
  ~BasicTrace();

  /// @brief Allow access to the header information and column filter.
  /// @return Read-only reference to the Data_Columns mapping.
  const Data_Columns &dcols() const { return m_dcols; }

  /** @brief Load job records from the configured input file.
   * @param[in] n_lines_to_read Maximum rows to load; zero means all rows.
   * @return `EXIT_SUCCESS` on success, otherwise a nonzero status. */
  int load_data(num_jobs_t n_lines_to_read = static_cast<num_jobs_t>(0u));

  /// Allow write access to the job trace data - for range-based
  /// iteration only (write_simulated_trace(), print(), etc.). Indexing
  /// this directly by job_no is wrong once anything's been reclaimed -
  /// use job_at(job_no) instead, which translates correctly.
  /// @return Mutable reference to the resident trace records.
  trace_data_t &data() { return m_data; }
  /// @brief Allow read-only access to the job trace data - see data() above.
  /// @return Read-only reference to the resident trace records.
  const trace_data_t &data() const { return m_data; }

  /**
   * @brief Look up a job by its permanent job_no (not a raw physical
   * index - those shift as reclaiming advances). The single choke point
   * for every job_no-based lookup; throws clearly rather than reading
   * garbage if job_no was already reclaimed or was never inserted.
   * @param[in] job_no Permanent job identifier.
   * @return Mutable reference to the corresponding record.
   */
  Job_Record &job_at(job_no_t job_no);
  /** @brief Look up a job by permanent identifier without allowing mutation.
   * @param[in] job_no Permanent job identifier.
   * @return Read-only reference to the corresponding record. */
  const Job_Record &job_at(job_no_t job_no) const;

  /// Jobs reclaimed so far - add to data().size() for the true total
  /// ever loaded (data().size() alone undercounts once anything's
  /// been reclaimed).
  /// @return Number of records reclaimed from the front of the job store.
  size_t num_reclaimed() const { return m_num_reclaimed; }

  /// Running stats over every is_scheduled() job seen so far via
  /// write_job_line() (both reclaim-time and the final flush) -
  /// correct even once some jobs have been reclaimed from m_data, unlike
  /// re-deriving these by iterating data() directly.
  /// @return Number of completed jobs included in running statistics.
  num_jobs_t completed_count() const { return m_completed_count; }
  /** @brief Return the accumulated wait time of emitted jobs.
   * @return Total wait time in seconds as tdiff_t. */
  tdiff_t wait_time_sum() const { return m_wait_time_sum; }
  /** @brief Return the accumulated turnaround time of emitted jobs.
   * @return Total turnaround time in seconds as tdiff_t. */
  tdiff_t turnaround_time_sum() const { return m_turnaround_time_sum; }
  /** @brief Return the latest completion time observed so far.
   * @return Latest completion time as sim_time_t. */
  sim_time_t makespan() const { return m_makespan; }

  /**
   * @brief Set the initial capacity for the job-record circular buffer.
   * Call before any processing begins - a no-op once the first job has
   * already been loaded (capacity is resolved lazily then, from the
   * job count if this was never called or was called with 0).
   * @param[in] capacity Requested entry capacity; zero selects automatic
   * sizing.
   */
  void set_job_store_capacity(size_t capacity) {
    m_job_store_capacity = capacity;
  }

  /// @brief Set the action taken when a full job store cannot reclaim.
  /// What to do if the buffer's full and the front job still isn't
  /// safe to reclaim (still running) - same convention as the wait queue.
  /// @param[in] policy Overflow action.
  void set_job_store_overflow(CircularOverflowPolicy policy) {
    m_job_store_overflow = policy;
  }

  /**
   * @brief Set the periodic completed-job flush interval in records.
   * @param[in] interval Number of processed departures per flush; zero uses
   *        the current job-store capacity.
   * @details Capacity pressure and explicit/final flushes take precedence and
   * reset the interval. The interval never changes job eligibility: only the
   * completed contiguous front prefix can be written and reclaimed.
   */
  /** @brief Processors-per-node divisor for SWF input; 0 derives it from the
   *  file header. No effect on other formats. */
  void set_cores_per_node(unsigned cores) { m_dcols.set_cores_per_node(cores); }

  void set_job_flush_interval(size_t interval) {
    m_job_flush_interval = interval;
  }

  /// @brief Configure the optional job-store memory-pressure limit.
  /// Enable/disable (and tune) the memory-pressure check that
  /// ensure_batch_capacity() (used by both append_jobs() and
  /// load_next_file()) runs before growing the job store for a new
  /// batch - see check_memory_pressure()'s own doc comment for the
  /// formula and rationale. fraction is the ceiling, as a fraction of
  /// actual available system memory, that projected job-store usage
  /// must stay under; 0.0 (the default) disables the check entirely.
  /// Values are trusted as given - no range validation here, matching
  /// set_job_store_overflow() above; the CLI (-m) and protobuf config
  /// (memory_pressure_fraction) layers validate before calling this.
  /// Off by default: unlike --job_store_capacity/--job_store_overflow
  /// (which bound a buffer size the caller chose), this queries actual
  /// system memory, which isn't something every caller wants tied to
  /// (e.g. containerized or memory-cgroup'd environments where
  /// /proc/meminfo may not reflect the effective limit).
  /// @param[in] fraction Fraction of available memory, or zero to disable.
  void set_memory_pressure_fraction(double fraction) {
    m_memory_pressure_fraction = fraction;
  }

  /**
   *  Run the trace from the begining to the end. i.e., run the simulation
   *  of 3 job events--submit, start, and end--in order to find out how many
   *  nodes were in use at the time of each job submission. This never
   *  consults a scheduler - begin_time/end_time are taken directly from
   *  the trace.
   *  @param[in] resource_trace_file Optional path to also write a
   *         time,free_nodes,allocated_nodes resource-occupancy trace; no
   *         such file is written if left empty.
   *  @param[in] total_nodes Pool size used only to derive free_nodes above.
   *  @param[in] max_time Inclusive event-time boundary. The default drains
   *         the trace completely.
   */
  void
  run_job_trace(const std::string &resource_trace_file = std::string(),
                num_nodes_t total_nodes = static_cast<num_nodes_t>(0u),
                sim_time_t max_time = std::numeric_limits<sim_time_t>::max());

  /**
   * @brief Record that the scheduler has started an existing job.
   *
   * Updates the Trace job record and creates its start/end events. This is
   * not scheduler-wait-queue insertion: Simulation::advance_to() calls it
   * only after SchedulerBase::schedule() selects a queued job to run.
   *
   * @param[in] job_idx Index of the existing job in m_data
   * @param[in] start_time Simulation time chosen by the scheduler
   * @see Simulation::submit_job()
   * @see SchedulerBase::insert_job()
   */
  void insert_job(job_no_t job_idx, sim_time_t start_time);

  /** Enqueue the recorded begin/end events of one replay-format job. */
  void enqueue_replay_job(job_no_t job_idx);

  /**
   * @brief Append a genuinely new job to m_data - the real streaming
   * insertion point (unlike insert_job()/submit_job(), which both
   * operate on a job already sitting in a preloaded m_data via
   * job_at()). Follows the same order established for this in
   * OUTPUT_TRACE_BUFFERS.md: check full(), try reclaim_front_jobs()
   * first, and only fall back to growing if nothing was reclaimable.
   * @param[in] current_time Current simulated time at a drained processing
   *        boundary, used by the reclaimability check.
   * @param[in] submit_time The new job's own submit_time attribute.
   * @param[in] num_nodes Number of nodes the job requests.
   * @param[in] queue Which queue the job was submitted to.
   * @param[in] limit_time User-estimated time limit.
   * @param[in] actual_run_time Known execution time, or empty to use
   *        limit_time as the streaming execution duration.
   * @return The new job's job_no. Simulation::append_job() immediately
   *         passes it to its protected submit_job() helper to enqueue it.
   * @see Simulation::append_job()
   * @see Simulation::submit_job()
   */
  job_no_t append_job(sim_time_t current_time, const epoch_t &submit_time,
                      num_nodes_t num_nodes, job_queue_t queue,
                      timeout_t limit_time,
                      std::optional<tdiff_t> actual_run_time = std::nullopt);

  /**
   * @brief Append several genuinely new jobs to m_data in one call -
   * the batch counterpart to append_job(), for the same never-seen-
   * before case (not a batch-preload; see load_data() for that).
   *
   * Progressive/multi-file loading does not use this network-facing record;
   * Trace::load_next_file() retains each parser-created Job_Record directly.
   * This structure remains the genuine streaming representation for jobs
   * that do not already exist in the trace.
   *
   * Fully all-or-nothing: nothing in this batch is appended unless
   * all of it can be. Two things are checked before m_data is
   * touched at all:
   *   - every request's ordering - requests must already be sorted
   *     by submit_time, non-decreasing - m_data's own sort invariant
   *     (see job_at()'s doc comment), otherwise silently and
   *     implicitly assumed across separate append_job() calls; a
   *     batch call is where this is easy to check explicitly, so it
   *     is. (This function does not itself check submit_time >=
   *     current_time - same as append_job(), that precondition is
   *     enforced one level up, by
   *     Simulation::append_job()/append_jobs().)
   *   - capacity for the *whole* batch - unlike append_job()'s
   *     per-job check-full()-then-reclaim-then-grow, which can only
   *     discover exhaustion one job at a time, this resolves
   *     reclaim_front_jobs()/growing against the batch's whole size
   *     up front (see reclaim_front_jobs()'s min_free parameter).
   *     A --job_store_overflow=abort failure is therefore also
   *     atomic: it happens before the batch's first push_back(), not
   *     mid-loop, so m_data is left exactly as it was, never
   *     partially filled.
   *
   * Once both checks pass, every request is copied into m_data with
   * no further capacity checks interleaved between them - the same
   * batch-then-copy shape load_data() already uses for its own bulk
   * insertion, just with reclaim resolved against the batch's size
   * too (load_data() never needs that: nothing is ever reclaimable at
   * load time). Deliberately not a per-request loop re-checking
   * full()/reclaim/grow on every iteration - resizing (and
   * reallocating/copying) the circular buffer once for the whole
   * batch, rather than up to once per request, is the reason this
   * exists as a batch call at all rather than a loop over
   * append_job() the caller could already write themselves.
   *
   * @param[in] current_time Current simulated time at a drained processing
   *        boundary, used by the reclaimability check.
   * @param[in] requests The new jobs' own data, in submit_time order.
   * @return Each new job's job_no, in the same order as requests.
   *         Simulation::append_jobs() immediately enqueues each one.
   * @see Simulation::append_jobs()
   * @see Simulation::submit_job()
   */
  std::vector<job_no_t>
  append_jobs(sim_time_t current_time,
              const std::vector<Job_Append_Request> &requests);

  /**
   * @brief Load one file's worth of jobs directly as Job_Record
   * objects (via the same load() free function load_data() uses),
   * for progressive/multi-file loading - a sequence of separate,
   * pre-sorted, pre-split trace files instead of one big one, so a
   * bounded --job_store_capacity can actually be honored (unlike
   * load_data(), which always grows to fit its one file whole).
   *
   * Deliberately does not go through append_jobs()/Job_Append_Request. This
   * takes the parser-created Job_Record objects directly, preserving their
   * complete input state and avoiding an unnecessary conversion.
   *
   * Two checks, both all-or-nothing before m_data is touched at all
   * (same shape as append_jobs()): this file's own rows must already
   * be sorted by submit_time (load_data() tolerates unsorted input by
   * sorting it in memory - this can't, since it never holds more than
   * one file's rows at a time), and its first job's submit_time must
   * be >= the previous call's last job's submit_time (continuity
   * across the file sequence - first call is exempt, nothing to
   * compare against yet).
   *
   * @param[in] current_time Current simulated time at a drained processing
   *        boundary, used by the reclaimability check.
   * @param[in] fname Path to this file - same trace format as any other
   *        input file (--infile), not the network-facing
   *        Job_Append_Request shape.
   * @return Each loaded job's job_no, in submit_time order. The
   *         progressive-loading path enqueues each one internally.
   */
  std::vector<job_no_t> load_next_file(sim_time_t current_time,
                                       const std::string &fname);

  /**
   * NEW SIMULATION API: Run simulation until (but not including) target time
   * Processes all events with time < target_time
   * @param[in] target_time Time to run until (exclusive)
   */
  void run_until_exclusive(sim_time_t target_time);

  /**
   * NEW SIMULATION API: Run simulation until and including target time
   * Processes all events with time <= target_time
   * @param[in] target_time Time to run until (inclusive)
   */
  void run_until_inclusive(sim_time_t target_time);

  /**
   * NEW SIMULATION API: Process exactly one event from the replay queue
   * Processes the earliest event in the queue, regardless of its time
   * @return true if an event was processed, false if queue was empty
   */
  bool process_single_event();

  /**
   * NEW SIMULATION API: Get current number of nodes in use
   * @return Number of nodes currently allocated
   */
  num_nodes_t get_nodes_in_use() const { return m_ctx.m_n_nodes_in_use; }

  /// True if a pAll (exclusive-access) job is currently running -
  /// same condition load_data()'s own submission loop uses to decide
  /// whether a newly-submitted job's set_busy_nodes() call should
  /// record total_nodes (the whole machine, since a pAll job
  /// monopolizes it) rather than get_nodes_in_use() (other jobs'
  /// actual current occupancy). Exposed so submit_job() - which
  /// submits one job at a time, outside load_data()'s own loop - can
  /// apply the exact same rule. Always false if MARK_DAT_PERIOD is
  /// off (m_pAll_cnt doesn't exist in that build - no DAT tracking
  /// means never in a DAT period, by definition).
  bool in_dat_period() const {
#if MARK_DAT_PERIOD
    return m_ctx.m_pAll_cnt > static_cast<num_jobs_t>(0u);
#else
    return false;
#endif
  }

  /// Read-only access to the pending-completion event queue, for a
  /// caller (Simulation) driving its own event loop against this Trace.
  const event_q_t &pending_events() const { return m_ctx.m_evtq; }

  /** @brief Print the job trace with simulation-derived fields.
   * @param[in,out] os Destination stream.
   * @return Reference to @p os. */
  std::ostream &print(std::ostream &os) const;

  /** @brief Print the total time span of the trace.
   * @param[in,out] os Destination stream.
   * @return Reference to @p os. */
  std::ostream &print_span(std::ostream &os) const;

  /**
   * @brief Set the initial capacity for the resource-history circular
   * buffer. Call before any processing begins - a no-op once the first
   * sample has already been recorded (capacity is resolved lazily then,
   * from m_data's size if this was never called or was called with 0).
   * @param[in] capacity Entry count; 0 = auto-size to the job trace, large
   *        enough that no entry needs reclaiming purely to make room.
   */
  void set_resource_history_capacity(size_t capacity) {
    m_resource_history_capacity = capacity;
  }

  /**
   * @brief Initialize incremental resource-history output.
   *
   * When Redis job output is active, resource CSV is written to
   * `<prefix>:resources:csv` and filename is ignored. Otherwise filename is
   * opened normally. Call once before processing begins.
   * @param[in] filename File output path when Redis is not active. An empty
   *        path disables file output.
   * @param[in] total_nodes Pool size, used to derive free_nodes at write time.
   * @param[in] msec Format timestamps with millisecond precision instead of
   *        truncating to whole seconds (matches Sim_Params::m_msec_output;
   *        tracer has no equivalent option, so it always leaves this false)
   */
  void start_resource_trace(const std::string &filename,
                            num_nodes_t total_nodes, bool msec = false);

  /**
   * Discard resource samples before a warm-start boundary while preserving
   * live occupancy and policy state. The next resource trace begins with a
   * baseline at start_time.
   */
  void reset_resource_recording(sim_time_t start_time) {
    m_ctx.m_resource_history.clear();
    m_resource_recording_start = start_time;
    const time_t sec = static_cast<time_t>(start_time);
    m_resource_recording_baseline =
        this->sample({sec, static_cast<float>(start_time - sec)},
                     m_ctx.m_n_nodes_in_use, m_resource_trace_current_capacity);
    m_has_resource_recording_baseline = true;
  }

  /**
   * @brief Write this Trace's recorded resource-occupancy history as CSV.
   * Redis simulation output uses `<prefix>:resources:csv`; otherwise this
   * writes a normal file. Both use the same schema and buffered data path.
   *
   * If start_resource_trace() was already called with the same filename,
   * this only flushes whatever's left buffered and closes the file -
   * everything reclaimed mid-run was already written incrementally. If
   * start_resource_trace() was never called, this opens filename fresh
   * and writes the buffer's current contents in one shot - the original,
   * pre-streaming behavior - but note anything reclaimed before this call
   * (from a capacity smaller than the trace) is then already gone.
   * @param[in] filename Output path; no-op if empty
   * @param[in] total_nodes Pool size, used to derive free_nodes at write time
   * @param[in] msec Format timestamps with millisecond precision instead of
   *        truncating to whole seconds (matches Sim_Params::m_msec_output;
   *        tracer has no equivalent option, so it always leaves this false)
   */
  void write_resource_trace(const std::string &filename,
                            num_nodes_t total_nodes, bool msec = false);

  /**
   * Record a time-varying capacity transition in the resource history.
   * Running allocation is unchanged; free nodes are clamped to zero when a
   * non-preemptive reduction temporarily leaves the system overcommitted.
   */
  void set_resource_capacity(sim_time_t time, num_nodes_t capacity);

  /**
   * Reset the effective resource capacity without recording a transition.
   * Simulation uses this at the start of each run so a reused Trace does not
   * retain the previous run's final scheduled capacity.
   */
  void reset_resource_capacity(num_nodes_t capacity) {
    m_resource_trace_current_capacity = capacity;
    m_resource_capacity_initialized = true;
  }

  /**
   * @brief Open filename early so a job's line gets written the moment
   * it's reclaimed from m_data, rather than only at the very end - same
   * reasoning as start_resource_trace() above. Call once, before any
   * processing begins.
   * @param[in] filename Output path; a no-op if empty - jobs reclaimed
   *        before write_simulated_trace() is later called are then
   *        simply discarded (lost), which defeats the whole point of
   *        this call, so this should always be paired with it once
   *        m_data can actually reclaim.
   * @param[in] msec Format timestamps with millisecond precision instead of
   *        truncating to whole seconds (matches Sim_Params::m_msec_output)
   * @param[in] redis_uri Redis connection URI; empty selects file output.
   * @param[in] redis_key_prefix Namespace for Redis output keys.
   */
  void start_simulated_trace(const std::string &filename, bool msec = false,
                             const std::string &redis_uri = {},
                             const std::string &redis_key_prefix = {});

  /**
   * @brief Write this Trace's job records to a CSV file (same format
   * write_simulated_trace() always used). If start_simulated_trace()
   * was already called with the same filename, this only writes
   * whatever jobs are still currently in m_data and closes the file -
   * everything reclaimed mid-run was already written at reclaim time.
   * If start_simulated_trace() was never called, opens filename fresh
   * and writes m_data's current contents in one shot - the original,
   * pre-streaming behavior.
   * @param[in] filename Output path; no-op if empty
   * @param[in] msec Format timestamps with millisecond precision instead of
   *        truncating to whole seconds
   * @param[in] completed_through Include only jobs completed at or before
   *        this time. The default includes every scheduled job.
   */
  void write_simulated_trace(
      const std::string &filename, bool msec = false,
      sim_time_t completed_through = std::numeric_limits<sim_time_t>::max());

  /**
   * @brief Explicitly write and reclaim the completed front prefix.
   *
   * This is the non-capacity-pressure trigger for long-running callers that
   * want to spread output work across the run. Every record is written at most
   * once through the permanent output cursor, complete lines are emitted in
   * blocks, and the underlying stream is flushed before return. Records whose
   * end_time is later than current_time remain resident and stop the front
   * sweep. The caller must first process every event through current_time.
   * @param[in] current_time Fully processed simulation time boundary.
   */
  void flush_completed_jobs(sim_time_t current_time);

#if MARK_DAT_PERIOD
  std::ostream &print_DAT(std::ostream &os);
#endif

#if MARK_DAT_PERIOD
  const reserved_t &get_reserved() const { return m_reserved; }
#endif

  /**
   * @brief Set default timezone for the trace
   * @param[in] tz_offset Timezone offset string (e.g., "-08:00", "+00:00")
   */
  void set_default_timezone(const std::string &tz_offset) {
    m_default_timezone = tz_offset;
  }

  /**
   * @brief Set timezone for a specific queue
   * @param[in] queue Queue name (e.g., "pbatch")
   * @param[in] tz_offset Timezone offset string
   */
  void set_queue_timezone(const std::string &queue,
                          const std::string &tz_offset) {
    m_queue_timezones[queue] = tz_offset;
  }

  /**
   * @brief Get timezone for a queue (returns default if not overridden)
   * @param[in] queue Queue name
   * @return Timezone offset string
   */
  std::string get_queue_timezone(const std::string &queue) const {
    auto it = m_queue_timezones.find(queue);
    return (it != m_queue_timezones.end()) ? it->second : m_default_timezone;
  }

protected:
  /** @brief Process queued resource events through a submission timestamp.
   * @param[in] t_sub Inclusive timestamp through which events are handled. */
  void process_events_until(const epoch_t &t_sub);

  /// Resolve m_resource_history_capacity (0 -> sized from m_data) and
  /// call m_ctx.m_resource_history.set_capacity() - once, the first time
  /// any sample is recorded. A no-op on every call after the first.
  void resolve_resource_history_capacity();

  /// Write every entry currently in m_ctx.m_resource_history to the active
  /// file or Redis resource sink in one batch, then clear the buffer.
  /// Used both for incremental reclaim-driven flushes during a run and
  /// for the final flush in write_resource_trace().
  void flush_resource_history();

  /// @brief Record one resource-occupancy sample.
  /// Record one (time, allocated) resource-occupancy sample - the single
  /// choke point process_events_until() and process_single_event() both
  /// go through, so reclaim-on-full only needs implementing once. If the
  /// buffer is full, this flushes+clears it first (every entry is always
  /// safe to reclaim here - see m_resource_history's own comment - so this
  /// always succeeds, unlike a wait queue or job store, which may need an
  /// abort/grow fallback when nothing is currently reclaimable).
  /// @param[in] time Sample timestamp.
  /// @param[in] allocated Number of allocated nodes.
  void record_resource_sample(const epoch_t &time, num_nodes_t allocated);

  /// @brief Resolve and apply the initial job-store capacity.
  /// Resolve m_job_store_capacity (0 -> sized from the job count) and
  /// call m_data.set_capacity() - once, the first time load_data() or
  /// insert_job() needs room. A no-op on every call after the first.
  /// @param[in] hint Expected job count used for automatic sizing.
  void resolve_job_store_capacity(num_jobs_t hint);

  /// @brief Test whether the front job-store slot is safe to reclaim.
  /// True when the front record is rejected, or its end_time is no later than
  /// current_time, its replay events (if any) have been enqueued, and no event
  /// at or before current_time remains queued. Together these form the local
  /// committed-time/GVT-style boundary for the record.
  /// @param[in] current_time Fully processed simulation time boundary.
  /// @return `true` when the front slot may be discarded.
  bool is_front_reclaimable(sim_time_t current_time) const;
  bool is_job_reclaimable(job_no_t job_id, const Job_Record &job,
                          sim_time_t current_time) const;

  /// Reclaim from the front while is_front_reclaimable() holds, advancing
  /// m_num_reclaimed, until at least min_free slots are free (capacity()
  /// - size() >= min_free) or nothing more is reclaimable. Guarded by
  /// that same condition on entry (not proactive - lazy reclaim, same
  /// as resource-history's record_resource_sample()): a no-op if
  /// already enough room. min_free defaults to 1, matching every
  /// existing single-job caller (append_job()) exactly;
  /// append_jobs() passes the whole batch's size, since "enough room
  /// for one more" isn't the right question when appending several at
  /// once.
  ///
  /// Reclamation is lazy: completion alone does not run a sweep. A sweep is
  /// invoked at record-insertion boundaries when space is needed, or by the
  /// explicit flush_completed_jobs() API. This avoids a reclaim decision on
  /// every completion while preserving bounded progressive/streaming storage.
  ///
  /// handle_overflow (default true) controls whether *this function*
  /// applies m_job_store_overflow (grow-or-abort) if still full()
  /// afterward - same fallback the wait queue already uses for the
  /// analogous case. append_job() (min_free=1) relies on this default,
  /// since it has no capacity check of its own afterward. append_jobs()
  /// passes false: it has its own batch-aware capacity check right
  /// after calling this, comparing against the batch's actual size
  /// rather than just m_data.full() - letting this function's fallback
  /// fire too would risk an extra, unnecessarily-small intermediate
  /// grow (or, for abort, a misleading single-job-shaped message) in a
  /// case only the batch caller can size and word correctly.
  /// @param[in] current_time Fully processed simulation time boundary.
  /// @param[in] min_free Minimum number of free slots required.
  /// @param[in] handle_overflow Whether to apply the configured fallback if
  /// reclamation cannot provide enough space.
  void reclaim_front_jobs(sim_time_t current_time, num_jobs_t min_free = 1,
                          bool handle_overflow = true);

  /// Return the configured periodic interval, resolving zero to the current
  /// circular-buffer capacity (with a one-record floor).
  size_t effective_job_flush_interval() const;

  /// Account for processed departures and perform an interval-triggered sweep
  /// at a fully drained event-time boundary.
  void maybe_flush_completed_jobs(sim_time_t current_time,
                                  size_t departures_processed);

  /// Write and remove the completed contiguous front prefix. `sync` requests
  /// an ostream flush for an explicit API boundary. Returns records removed.
  size_t flush_completed_jobs_impl(sim_time_t current_time, bool sync);

  /// Shared by append_jobs() and load_next_file(): ensures
  /// m_data has room for batch_size more entries, reclaiming first
  /// and growing (or aborting, per m_job_store_overflow) only if
  /// still short after that - extracted out of append_jobs() so both
  /// callers get this exact logic once, not duplicated. Mutates
  /// m_data's capacity only, never inserts anything itself - callers
  /// still do their own push_back()ing once this returns.
  /// @param[in] current_time Current simulation time.
  /// @param[in] batch_size Number of records about to be appended.
  void ensure_batch_capacity(sim_time_t current_time, size_t batch_size);

  /// Called by ensure_batch_capacity() (after reclaiming, before
  /// deciding whether to grow) when m_memory_pressure_fraction > 0.0.
  /// Throws std::runtime_error if taking on batch_size more jobs
  /// would push projected peak job-store memory past that fraction of
  /// actual available system memory - a check against real memory
  /// pressure, independent of whatever --job_store_capacity happens
  /// to be set to (capacity bounds a buffer size the caller chose;
  /// this bounds against the machine's actual, current constraint
  /// instead).
  ///
  /// Formula, in units of sizeof(Job_Record): let old_cap =
  /// m_data.capacity() and new_cap = the same doubling computation
  /// ensure_batch_capacity()'s own grow step would use to reach
  /// old_cap + batch_size - not a fixed multiplier guess, the exact
  /// target capacity growing would actually allocate, however many
  /// doublings that takes for a large batch_size. If new_cap >
  /// old_cap (a grow would happen), projected usage is old_cap +
  /// new_cap + batch_size: boost::circular_buffer::set_capacity()
  /// allocates the new buffer and copies every existing entry into it
  /// before freeing the old one, so both are resident simultaneously
  /// mid-copy; the extra + batch_size accounts for load_next_file()'s
  /// own temporary std::vector<Job_Record> (the whole file's rows,
  /// read before this call, not yet moved into m_data) being resident
  /// at the exact moment this check runs - append_jobs() has no such
  /// vector (its Job_Append_Request is a different, smaller struct
  /// until pushed), so counting it uniformly for both callers only
  /// makes the check more conservative for append_jobs(), never less.
  /// If no grow would happen (new_cap == old_cap), projected usage is
  /// just old_cap + batch_size - the existing allocation, plus that
  /// same temporary vector.
  ///
  /// This is compared against m_memory_pressure_fraction * (available
  /// memory) / sizeof(Job_Record). The fraction leaves headroom for
  /// everything else the process holds (the rest of the trace
  /// machinery, the wait queue, resource history, and any headroom
  /// the OS/other processes need) - job records aren't the only
  /// consumer of memory here, just the one this check can reason
  /// about.
  ///
  /// If available memory can't be determined at all (see
  /// get_available_memory_bytes()'s own doc comment for when that
  /// happens), this is a no-op: there's nothing to enforce against,
  /// and refusing to proceed on missing information would be worse
  /// than not checking at all.
  /// @param[in] batch_size Number of incoming records included in the
  /// projected peak.
  void check_memory_pressure(size_t batch_size) const;

  /// Add one scheduled job to the active file or Redis flush buffer.
  /// Unscheduled/rejected jobs were never written by the original
  /// write_simulated_trace() either. This is the single choke point both
  /// reclaim_front_jobs() and write_simulated_trace() go through.
  /// @param[in] job Record to write and include in running statistics.
  void write_job_line(const Job_Record &job);

  /// Send an existing trace-buffer range to the active output sink.
  void write_job_range(typename trace_data_t::const_iterator first,
                       typename trace_data_t::const_iterator last,
                       job_no_t first_job_id, sim_time_t completed_through);

  /// Return whether either the file or Redis simulated-trace sink is active.
  bool simulated_trace_output_active() const;

  /// Write and clear file lines accumulated by one reclaim/final-write
  /// operation. If sync is true, also flush the underlying ostream buffer.
  void flush_simulated_trace_buffer(bool sync);

  /** @brief Reset policy-owned runtime aggregates before checkpoint restore. */
  void reset_policy_runtime_state() { this->reset_runtime_state(); }

  /**
   * @brief Rebuild policy-owned runtime aggregates for one restored job.
   * @param[in] job Restored job that is currently running.
   */
  void restore_policy_running_job(const typename Policy::record_type &job) {
    this->on_start(job);
  }
};

/// Pcon trace: carries power values in both job and resource records.
using PconTrace = BasicTrace<Pcon_Trace_Policy>;
/// Normal build: existing compact record layouts and behavior are unchanged.
using Trace = BasicTrace<Standard_Trace_Policy>;
/// Standard Workload Format input (see Swf_Trace_Policy).
using SwfTrace = BasicTrace<Swf_Trace_Policy>;

extern template class BasicTrace<Standard_Trace_Policy>;
extern template class BasicTrace<Pcon_Trace_Policy>;
extern template class BasicTrace<Swf_Trace_Policy>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_TRACE_HPP
