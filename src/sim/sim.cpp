/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file sim/sim.cpp
 * @brief Discrete-event simulation orchestration implementation.
 * @details Public Simulation contracts are documented in sim.hpp; this file
 * contains event-loop sequencing, runtime sampling, and output mechanics.
 */

#include "sim/sim.hpp"
#include "sim/block_wait_queue.hpp"
#include "trace/epoch.hpp"
#include "trace/job_io.hpp"
#include "trace/parse_utils.hpp"
#if defined(DR_EVT_HAS_SER20)
#include "utils/state_io_ser20.hpp"
#endif
#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
#include "utils/redis_output.hpp"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <type_traits>
#include <typeinfo>

namespace dr_evt {

namespace {

constexpr tdiff_t bounded_slowdown_threshold = 10.0;

/** Validate and convert a streaming wall-time request.
 * @param[in] limit_time Requested wall time in seconds.
 * @return The equivalent integral scheduler limit.
 * @throws std::invalid_argument if limit_time is non-positive, non-finite,
 * fractional, or outside the range of timeout_t.
 */
timeout_t checked_streaming_limit(tdiff_t limit_time) {
  if (!std::isfinite(limit_time) || limit_time <= 0.0 ||
      std::trunc(limit_time) != limit_time ||
      limit_time > static_cast<double>(std::numeric_limits<timeout_t>::max())) {
    throw std::invalid_argument(
        "limit_time must be a positive whole number of seconds");
  }
  return static_cast<timeout_t>(limit_time);
}

#if defined(DR_EVT_HAS_SER20)

constexpr std::array<char, 8> checkpoint_magic{'D', 'R', 'E', 'V',
                                               'T', 'C', 'K', 'P'};
constexpr std::uint32_t checkpoint_version = 9; // 9: SWF trace kind/records

/// Trace policy identifier stored in a checkpoint so a file written by one
/// trace type is not restored into another.
template <typename TraceType> constexpr std::uint8_t checkpoint_trace_kind() {
  if constexpr (std::is_same_v<TraceType, PconTrace>) {
    return 1u;
  } else if constexpr (std::is_same_v<TraceType, SwfTrace>) {
    return 2u;
  } else {
    return 0u;
  }
}
constexpr std::uint64_t checkpoint_collection_limit = 100000000;
constexpr std::uint64_t checkpoint_string_limit = 64 * 1024 * 1024;

/** Output-only Redis state sampled when a checkpoint is explicitly written. */
struct CheckpointRedisBoundary {
  std::uint64_t job_csv_bytes = 0;
  std::uint64_t resource_csv_bytes = 0;
  std::uint64_t resource_count = 0;
  std::vector<std::string> job_ids;
  bool resource_trace_active = false;
};

/** Thin typed facade over one Ser20 checkpoint output archive. */
class CheckpointWriter {
public:
  /** @brief Start an archive on a caller-owned output stream. */
  explicit CheckpointWriter(std::ostream &output) : m_archive(output) {}

  /** @brief Write an exact number of uninterpreted bytes. */
  void bytes(const void *data, size_t size) {
    m_archive.saveBinary(data, static_cast<std::streamsize>(size));
  }

  /** @brief Archive one trivially copyable scalar value. */
  template <typename T> void value(const T &item) {
    static_assert(std::is_trivially_copyable_v<T>);
    m_archive(item);
  }

  /** @brief Archive one value through its Ser20 serialization contract. */
  template <typename T> void object(const T &item) { m_archive(item); }

  /** @brief Archive an enumeration through its underlying integer type. */
  template <typename Enum> void enumeration(Enum item) {
    using Underlying = std::underlying_type_t<Enum>;
    value(static_cast<Underlying>(item));
  }

  /** @brief Archive a bounded-length string and its byte count. */
  void string(const std::string &text) {
    value(static_cast<std::uint64_t>(text.size()));
    bytes(text.data(), text.size());
  }

private:
  ser20::BinaryOutputArchive m_archive;
};

/** Thin validating facade over one Ser20 checkpoint input archive. */
class CheckpointReader {
public:
  /** @brief Start an archive on a caller-owned input stream. */
  explicit CheckpointReader(std::istream &input) : m_archive(input) {}

  /** @brief Read an exact number of uninterpreted bytes. */
  void bytes(void *data, size_t size) {
    m_archive.loadBinary(data, static_cast<std::streamsize>(size));
  }

  /** @brief Read and return one trivially copyable scalar value. */
  template <typename T> T value() {
    static_assert(std::is_trivially_copyable_v<T>);
    T item{};
    m_archive(item);
    return item;
  }

  /** @brief Restore one value through its Ser20 serialization contract. */
  template <typename T> void object(T &item) { m_archive(item); }

  /** @brief Read an enumeration through its underlying integer type. */
  template <typename Enum> Enum enumeration() {
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<Enum>(value<Underlying>());
  }

  /** @brief Read a collection count after applying the allocation guard. */
  size_t count() {
    const auto result = value<std::uint64_t>();
    if (result > checkpoint_collection_limit ||
        result > std::numeric_limits<size_t>::max()) {
      throw std::runtime_error("simulation checkpoint collection is too large");
    }
    return static_cast<size_t>(result);
  }

  /** @brief Read a string after applying the allocation guard. */
  std::string string() {
    const auto size = value<std::uint64_t>();
    if (size > checkpoint_string_limit ||
        size > std::numeric_limits<size_t>::max()) {
      throw std::runtime_error("simulation checkpoint string is too large");
    }
    std::string result(static_cast<size_t>(size), '\0');
    bytes(result.data(), result.size());
    return result;
  }

private:
  ser20::BinaryInputArchive m_archive;
};

/** @brief Archive DR_EVT's whole-second plus fractional-second timestamp. */
void write_epoch(CheckpointWriter &writer, const epoch_t &time) {
  writer.value(time.first);
  writer.value(time.second);
}

/** @brief Restore DR_EVT's whole-second plus fractional-second timestamp. */
epoch_t read_epoch(CheckpointReader &reader) {
  return {reader.value<time_t>(), reader.value<float>()};
}

/** @brief Reject a checkpoint created with incompatible configuration. */
template <typename T>
void require_checkpoint_match(const T &actual, const T &expected,
                              const char *name) {
  if (actual != expected) {
    throw std::runtime_error(
        std::string("checkpoint configuration mismatch: ") + name);
  }
}

/** Return the byte boundary of a flushed checkpoint output stream. */
std::uint64_t checkpoint_output_bytes(std::ofstream &output,
                                      const char *description) {
  const auto position = output.tellp();
  if (position < 0) {
    throw std::runtime_error(std::string("cannot determine ") + description +
                             " checkpoint boundary");
  }
  return static_cast<std::uint64_t>(position);
}

/** Preserve pre-restart output and open a header-only resumed segment. */
void start_restart_output_segment(const std::string &filename,
                                  std::uint64_t checkpoint_bytes,
                                  std::ofstream &output) {
  namespace fs = std::filesystem;
  const fs::path active(filename);
  std::ifstream existing(active, std::ios::binary);
  if (!existing) {
    throw std::runtime_error("checkpoint output is missing: " + filename);
  }
  std::string header;
  std::getline(existing, header);
  header += '\n';
  existing.close();
  if (header.size() > checkpoint_bytes ||
      fs::file_size(active) < checkpoint_bytes) {
    throw std::runtime_error(
        "checkpoint output is shorter than its saved boundary: " + filename);
  }

  fs::path archived;
  for (std::uint64_t generation = 1;; ++generation) {
    archived = filename + ".pre-restart." + std::to_string(generation);
    if (!fs::exists(archived) &&
        !fs::exists(archived.string() + ".checkpoint-bytes")) {
      break;
    }
  }

  fs::rename(active, archived);
  try {
    std::ofstream boundary(archived.string() + ".checkpoint-bytes");
    boundary << checkpoint_bytes << '\n';
    boundary.close();
    if (!boundary) {
      throw std::runtime_error("cannot record checkpoint boundary for: " +
                               archived.string());
    }
    output.open(active, std::ios::binary | std::ios::trunc);
    output << header;
    output.flush();
    if (!output) {
      throw std::runtime_error("cannot create post-restart output: " +
                               filename);
    }
  } catch (...) {
    output.close();
    std::error_code ignored;
    fs::remove(active, ignored);
    fs::remove(archived.string() + ".checkpoint-bytes", ignored);
    fs::rename(archived, active, ignored);
    throw;
  }
}

#endif

} // namespace

template <typename TraceType>
BasicSimulation<TraceType>::BasicSimulation(Sim_Params params)
    : m_params(std::move(params)),
      m_trace(m_params.m_infile, m_params.m_trace_format,
              m_params.m_timestamp_format, m_params.m_timezone),
      m_scheduler(create_scheduler(
          m_params.m_total_nodes, m_trace.data().size(),
          m_params.m_backfill_policy, m_params.m_priority_policy,
          m_params.m_queue_impl, m_params.m_block_size,
          m_params.m_wait_queue_capacity, m_params.m_wait_queue_overflow)),
      m_custom_scheduler(nullptr), m_current_time(0.0),
      m_job_rejection_capacity(m_params.m_total_nodes),
      m_capacity_changes(load_capacity_schedule(m_params.m_capacity_schedule,
                                                m_params.m_total_nodes)),
      m_next_capacity_change(0), m_current_capacity(m_params.m_total_nodes),
      m_capacity_area(0.0), m_capacity_area_time(0.0), m_jobs_completed(0),
      m_jobs_submitted(0), m_jobs_started(0),
      m_latest_submit_time(-std::numeric_limits<sim_time_t>::infinity()),
      m_next_progressive_file(0),
      m_checkpoint_loaded(false), m_last_automatic_checkpoint_jobs(0),
      m_has_automatic_checkpoint(false),
      m_last_automatic_checkpoint_time(0.0),
      m_pre_start_jobs(0), m_warm_resource_area(0.0),
      m_warm_resource_end(0.0), m_rng(m_params.m_seed), m_queue_length_sum(0),
      m_queue_length_samples(0), m_queue_length_peak(0) {
  m_trace.set_cores_per_node(m_params.m_cores_per_node);
  reset_capacity_schedule();
}

template <typename TraceType>
BasicSimulation<TraceType>::BasicSimulation(Sim_Params params,
                                            job_cost_function_t cost_function,
                                            backfill_selector_t selector)
    : m_params(std::move(params)),
      m_trace(m_params.m_infile, m_params.m_trace_format,
              m_params.m_timestamp_format, m_params.m_timezone),
      m_scheduler(std::make_unique<CustomFCFSScheduler>(
          m_params.m_total_nodes, m_trace.data().size(),
          m_params.m_backfill_policy, m_params.m_num_max_candidates,
          std::move(cost_function),
          std::move(selector), m_params.m_wait_queue_capacity,
          m_params.m_wait_queue_overflow)),
      m_custom_scheduler(static_cast<CustomFCFSScheduler *>(m_scheduler.get())),
      m_current_time(0.0), m_job_rejection_capacity(m_params.m_total_nodes),
      m_capacity_changes(load_capacity_schedule(m_params.m_capacity_schedule,
                                                m_params.m_total_nodes)),
      m_next_capacity_change(0), m_current_capacity(m_params.m_total_nodes),
      m_capacity_area(0.0), m_capacity_area_time(0.0), m_jobs_completed(0),
      m_jobs_submitted(0), m_jobs_started(0),
      m_latest_submit_time(-std::numeric_limits<sim_time_t>::infinity()),
      m_next_progressive_file(0),
      m_checkpoint_loaded(false), m_last_automatic_checkpoint_jobs(0),
      m_has_automatic_checkpoint(false),
      m_last_automatic_checkpoint_time(0.0),
      m_pre_start_jobs(0), m_warm_resource_area(0.0),
      m_warm_resource_end(0.0), m_rng(m_params.m_seed), m_queue_length_sum(0),
      m_queue_length_samples(0), m_queue_length_peak(0) {
  m_trace.set_cores_per_node(m_params.m_cores_per_node);
  reset_capacity_schedule();
}

template <typename TraceType>
BasicSimulation<TraceType>::BasicSimulation(
    Sim_Params params, std::unique_ptr<CustomFCFSScheduler> scheduler)
    : m_params(std::move(params)),
      m_trace(m_params.m_infile, m_params.m_trace_format,
              m_params.m_timestamp_format, m_params.m_timezone),
      m_scheduler(std::move(scheduler)),
      m_custom_scheduler(static_cast<CustomFCFSScheduler *>(m_scheduler.get())),
      m_current_time(0.0), m_job_rejection_capacity(m_params.m_total_nodes),
      m_capacity_changes(load_capacity_schedule(m_params.m_capacity_schedule,
                                                m_params.m_total_nodes)),
      m_next_capacity_change(0), m_current_capacity(m_params.m_total_nodes),
      m_capacity_area(0.0), m_capacity_area_time(0.0), m_jobs_completed(0),
      m_jobs_submitted(0), m_jobs_started(0),
      m_latest_submit_time(-std::numeric_limits<sim_time_t>::infinity()),
      m_next_progressive_file(0),
      m_checkpoint_loaded(false), m_last_automatic_checkpoint_jobs(0),
      m_has_automatic_checkpoint(false),
      m_last_automatic_checkpoint_time(0.0),
      m_pre_start_jobs(0), m_warm_resource_area(0.0),
      m_warm_resource_end(0.0), m_rng(m_params.m_seed), m_queue_length_sum(0),
      m_queue_length_samples(0), m_queue_length_peak(0) {
  if (!m_scheduler) {
    throw std::invalid_argument("custom scheduler must not be null");
  }
  m_trace.set_cores_per_node(m_params.m_cores_per_node);
  reset_capacity_schedule();
}

template <typename TraceType> void BasicSimulation<TraceType>::run() {
  if (m_params.m_checkpoint_file.empty() &&
      m_params.m_checkpoint_interval_jobs != 0) {
    throw std::invalid_argument(
        "checkpoint_interval_jobs requires checkpoint_file");
  }
  if (m_params.m_verbose) {
    std::cout << "Starting simulation..." << std::endl;
  }

  if (m_params.m_is_time_set &&
      (!std::isfinite(m_params.m_max_time) || m_params.m_max_time < 0.0)) {
    throw std::invalid_argument("--max_time must be finite and nonnegative");
  }
  if (m_params.m_is_time_set &&
      m_params.m_max_time < m_params.m_sim_start_time) {
    throw std::invalid_argument(
        "--max_time must be greater than or equal to --sim_start_time");
  }

  // Must happen before initialize_trace()/run_progressive() (either
  // resolves m_data's capacity from whatever's set here) - unlike
  // resource-history's capacity, which is only needed once recording
  // starts, well after load.
  m_trace.set_job_store_capacity(m_params.m_job_store_capacity);
  m_trace.set_job_store_overflow(m_params.m_job_store_overflow);
  m_trace.set_job_flush_interval(m_params.m_job_flush_interval);
  m_trace.set_memory_pressure_fraction(m_params.m_memory_pressure_fraction);

  if (!m_params.m_infile_list.empty()) {
    if (m_params.m_sim_start_time != 0.0) {
      throw std::runtime_error(
          "--sim_start_time is not supported with --infile_list; warm-start "
          "classification requires one replay trace loaded at the boundary");
    }
    // Progressive loading: REPLAY-format input isn't supported here
    // - REPLAY bypasses the scheduler entirely (begin_time/end_time
    // already fixed in the trace), so there's no notion of "submit
    // this job now" for it to plug into in the first place, and
    // nothing to gain from bounding memory during a run that never
    // makes scheduling decisions at all.
    if (m_trace.dcols().get_trace_mode() == TraceMode::REPLAY) {
      throw std::runtime_error(
          "--infile_list does not support REPLAY-format input "
          "(begin_time/end_time already present) - it only makes "
          "sense for simulation-mode input, where the scheduler "
          "actually decides when each job runs.");
    }
    run_progressive();
    if (m_params.m_verbose) {
      std::cout << "Simulation complete\n" + std::string("Jobs submitted: ") +
                       std::to_string(m_jobs_submitted) + "\n" +
                       std::string("Jobs started: ") +
                       std::to_string(m_jobs_started) + "\n" +
                       std::string("Jobs completed: ") +
                       std::to_string(m_jobs_completed) + "\n";
    }
    return;
  }

  // Initialize: load jobs and determine durations
  initialize_trace();

  if (m_params.m_verbose) {
    std::cout << "Loaded " + std::to_string(m_trace.data().size()) +
                     " jobs from trace\n";
    std::cout << "Running simulation with " +
                     std::to_string(m_params.m_total_nodes) + " nodes\n";
  }

  m_trace.set_resource_history_capacity(m_params.m_resource_history_capacity);

  if (m_params.m_sim_start_time != 0.0) {
    run_warm_start();
  } else {
    // Open outputs before processing so bounded buffers can flush
    // incrementally. Warm-start opens them later, after discarding pre-boundary
    // samples and installing its nonzero baseline.
    m_trace.start_simulated_trace(m_params.get_outfile(),
                                  m_params.m_msec_output, m_params.m_redis_uri,
                                  m_params.m_redis_key_prefix);
    m_trace.start_resource_trace(m_params.get_resource_trace(),
                                 m_params.m_total_nodes,
                                 m_params.m_msec_output);

    if (m_trace.dcols().get_trace_mode() == TraceMode::REPLAY) {
      // Replay-format input (begin_time/end_time present): don't consult
      // the scheduler at all - reuse the same bypass logic the standalone
      // tracer binary uses, driven into Trace's own owned context so the rest
      // of this class (write_simulated_trace(), write_resource_trace())
      // sees the result exactly as if the scheduler had run.
      const sim_time_t run_limit = m_params.m_is_time_set
                                       ? m_params.m_max_time
                                       : std::numeric_limits<sim_time_t>::max();
      sim_time_t replay_end = m_current_time;
      job_no_t replay_job_no = static_cast<job_no_t>(m_trace.num_reclaimed());
      for (const auto &job : m_trace.data()) {
        const sim_time_t submit =
            convert_epoch<sim_time_t>(job.get_submit_time());
        const sim_time_t begin =
            convert_epoch<sim_time_t>(job.get_begin_time());
        const sim_time_t end = convert_epoch<sim_time_t>(job.get_end_time());
        if (submit <= run_limit) {
          replay_end = std::max(replay_end, end);
          if (end <= run_limit) {
            ++m_jobs_completed;
          } else if (m_params.m_is_time_set && begin <= run_limit) {
            m_running_jobs[replay_job_no] = {
                begin, static_cast<tdiff_t>(end - begin), job.get_num_nodes()};
          }
        }
        ++replay_job_no;
      }
      m_trace.run_job_trace(std::string(), m_params.m_total_nodes, run_limit);
      m_current_time = m_params.m_is_time_set ? run_limit : replay_end;
    } else {
      // Batch mode: Submit all jobs upfront, then advance to infinity
      // This uses the streaming API internally
      for (num_jobs_t i = 0; i < m_trace.data().size(); ++i) {
        const auto &job = m_trace.job_at(i);
        sim_time_t submit_time =
            convert_epoch<sim_time_t>(job.get_submit_time());
        submit_job(i, submit_time);
      }

      // A configured maximum is an inclusive event-time boundary. Without
      // one, use the internal drain sentinel and stop at the last real event.
      const sim_time_t run_limit = m_params.m_is_time_set
                                       ? m_params.m_max_time
                                       : std::numeric_limits<sim_time_t>::max();
      advance_to(run_limit);
    }
  }

  // m_jobs_completed is tracked incrementally during the run itself
  // (see the event-processing loop above) - no need to recompute it
  // here, and doing so by iterating m_trace.data() directly would
  // now be wrong anyway, since reclaimed jobs are no longer there to
  // recount.
  if (m_params.m_verbose) {
    std::cout << "Simulation complete\n" + std::string("Jobs submitted: ") +
                     std::to_string(m_jobs_submitted) + "\n" +
                     std::string("Jobs started: ") +
                     std::to_string(m_jobs_started) + "\n" +
                     std::string("Jobs completed: ") +
                     std::to_string(m_jobs_completed) + "\n";
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::print_stats(std::ostream &os) const {
  os << "=== Simulation Statistics ===" << std::endl;
  os << "Total jobs: ";
  if (m_params.m_sim_start_time != 0.0) {
    os << m_jobs_submitted;
  } else {
    os << (m_trace.data().size() + m_trace.num_reclaimed());
  }
  os << std::endl;
  os << "Jobs submitted: " << m_jobs_submitted << std::endl;
  os << "Jobs started: " << m_jobs_started << std::endl;
  // m_trace.completed_count() (populated via write_job_line(), called
  // both at reclaim time and by write_simulated_trace()'s final
  // flush - already run by the time this is called, see sim.cpp's
  // caller) counts every completed job regardless of whether it's
  // since been reclaimed from m_data - m_jobs_completed only tracks
  // in-flight completions during the run itself and isn't used here.
  os << "Jobs completed: " << m_trace.completed_count() << std::endl;
  os << "Current time: "
     << format_sim_time(m_current_time, m_params.m_msec_output) << std::endl;
  os << "Total nodes: " << m_params.m_total_nodes << std::endl;

  // Calculate metrics - sum+count already accumulated incrementally in
  // Trace as each job was written out (see write_job_line()), so this
  // is correct even for jobs already reclaimed from m_data by now.
  if (m_trace.completed_count() > 0) {
    const auto completed = m_trace.completed_count();

    // Unlike Current time/Makespan above, these are computed averages
    // (division results), which commonly have a fractional part even
    // with integer-second input data (e.g. 220/3 = 73.333...) - that
    // precision is meaningful and was shown by default before
    // msec_output existed, so it's preserved here regardless of
    // msec_output's setting, rather than routed through
    // format_sim_time (whose integer-truncation default is for
    // matching existing trace-output files' conventions, not for
    // these summary statistics).
    os << "Average wait time: " << (m_trace.wait_time_sum() / completed)
       << " sec" << std::endl;
    os << "Average turnaround time: "
       << (m_trace.turnaround_time_sum() / completed) << " sec" << std::endl;
    os << "Makespan: "
       << format_sim_time(m_trace.makespan(), m_params.m_msec_output) << " sec"
       << std::endl;
  }

  // Queue length statistics
  if (m_queue_length_samples > 0) {
    double avg_queue_length =
        static_cast<double>(m_queue_length_sum) / m_queue_length_samples;
    os << "Average queue length: " << avg_queue_length << " jobs" << std::endl;
    os << "Peak queue length: " << m_queue_length_peak << " jobs" << std::endl;
  }
}

template <typename TraceType>
num_jobs_t BasicSimulation<TraceType>::initialize_trace(num_jobs_t max_jobs) {
  // Clear any previously-loaded data first, so this method is safe to
  // call more than once (directly, or via run() after an earlier
  // explicit call - run() calls this internally too). Without this,
  // Job_Io::load() only ever push_back()s and never clears the
  // underlying vector itself, so a second call would silently append
  // to, rather than replace, the first call's jobs - e.g. calling
  // initialize_trace() explicitly and then run() would silently double
  // every job's count.
  m_trace.data().clear();

  // Load trace data
  const auto max_num_jobs =
      (max_jobs > 0u) ? max_jobs
                      : (m_params.m_is_jobs_set ? m_params.m_max_jobs
                                                : static_cast<num_jobs_t>(0u));

  // No .reserve() here anymore - load_data() sizes m_data's capacity
  // itself (resolve_job_store_capacity()), same convention as the wait
  // queue and resource-history.
  int rc = m_trace.load_data(max_num_jobs);
  if (rc != EXIT_SUCCESS) {
    throw std::runtime_error("Failed to load trace data");
  }

  // Sort jobs by submission time
  std::stable_sort(m_trace.data().begin(), m_trace.data().end());

  // Determine actual durations (simulation mode only)
  if (m_trace.dcols().get_trace_mode() == TraceMode::SIMULATION) {
    determine_job_run_time();
  }

  m_current_time = 0.0;
  reset_capacity_schedule();
  m_jobs_submitted = 0;
  m_jobs_started = 0;
  m_latest_submit_time = -std::numeric_limits<sim_time_t>::infinity();
  m_jobs_completed = 0;
  m_pre_start_jobs = 0;
  m_warm_resource_area = 0.0;
  m_warm_resource_end = 0.0;
  m_first_appended_job_idx = std::numeric_limits<job_no_t>::max();
  m_appended_job_query_records.clear();
  if (m_custom_scheduler != nullptr) {
    m_custom_scheduler->reset_resource_accounting();
  }

  return static_cast<num_jobs_t>(m_trace.data().size());
}

template <typename TraceType>
void BasicSimulation<TraceType>::run_warm_start() {
  const sim_time_t sim_start_time = m_params.m_sim_start_time;
  const sim_time_t run_limit = m_params.m_is_time_set
                                   ? m_params.m_max_time
                                   : std::numeric_limits<sim_time_t>::max();
  if (m_trace.dcols().get_trace_mode() != TraceMode::REPLAY) {
    throw std::runtime_error(
        "--sim_start_time requires replay-format input with begin_time and "
        "end_time columns so jobs already running at the boundary can be "
        "identified");
  }

  const job_no_t first_job = static_cast<job_no_t>(m_trace.num_reclaimed());
  const job_no_t jobs_end =
      static_cast<job_no_t>(m_trace.num_reclaimed() + m_trace.data().size());
  m_pre_start_jobs = 0;
  m_warm_resource_area = 0.0;
  m_warm_resource_end = sim_start_time;

  for (job_no_t job_no = first_job; job_no < jobs_end; ++job_no) {
    auto &job = m_trace.job_at(job_no);
    const sim_time_t begin = convert_epoch<sim_time_t>(job.get_begin_time());
    const sim_time_t end = convert_epoch<sim_time_t>(job.get_end_time());
    const sim_time_t submit = convert_epoch<sim_time_t>(job.get_submit_time());

    if (begin < sim_start_time) {
      // Historical starts bypass the scheduler. Replay establishes both live
      // allocation and policy-specific state (notably Pcon) exactly once.
      m_trace.enqueue_replay_job(job_no);
      ++m_pre_start_jobs;
      if (end > sim_start_time) {
        // Its departure is fixed historical state, so reservations should
        // use that known end rather than a possibly stale original limit.
        m_running_jobs[job_no] = {begin, job.get_actual_run_time(),
                                  job.get_num_nodes()};
        m_warm_resource_area +=
            static_cast<tdiff_t>(job.get_num_nodes()) * (end - sim_start_time);
        m_warm_resource_end = std::max(m_warm_resource_end, end);
      }
    } else if (submit >= sim_start_time) {
      // Let the normal scheduler replace the historical begin/end times in
      // the counterfactual run, and select this simulated job's duration by
      // the same run-time policy used in an ordinary simulation. Warmup jobs
      // bypass this branch and retain their recorded timing.
      job.prepare_for_resimulation();
      determine_one_job_run_time(job);
    } else {
      // This job was waiting before the boundary. Reconstructing an inherited
      // wait queue is a separate policy decision, so it is intentionally not
      // admitted into either the seed state or the post-boundary workload.
      job.suppress_output();
    }
  }

  // Replay only the history required to establish state at t. Old departures
  // are suppressed before Trace can run any output/reclamation hook.
  while (!m_trace.pending_events().empty()) {
    const auto event = *m_trace.pending_events().begin();
    const sim_time_t event_time = convert_epoch<sim_time_t>(event.get_time());
    if (event_time > sim_start_time) {
      break;
    }
    if (event.is_departure()) {
      m_trace.job_at(event.get_job_idx()).suppress_output();
      --m_pre_start_jobs;
    }
    m_trace.process_single_event();
  }

  m_current_time = sim_start_time;
  apply_capacity_changes(sim_start_time);
  reset_capacity_accounting(sim_start_time);

  // This is the accounting/output boundary: retain occupancy and Pcon state,
  // discard every pre-t sample, and establish a baseline at t.
  m_trace.reset_resource_recording(sim_start_time);
  m_trace.start_simulated_trace(m_params.get_outfile(), m_params.m_msec_output,
                                m_params.m_redis_uri,
                                m_params.m_redis_key_prefix);
  m_trace.start_resource_trace(m_params.get_resource_trace(),
                               m_params.m_total_nodes, m_params.m_msec_output);
  if (m_custom_scheduler != nullptr) {
    m_custom_scheduler->reset_resource_accounting(sim_start_time,
                                                  get_available_nodes());
  }

  // No auxiliary job list is retained: prepared post-boundary records keep
  // their real submission time, while every excluded/finished historical
  // record carries the existing unscheduled sentinel.
  for (job_no_t job_no = first_job; job_no < jobs_end; ++job_no) {
    const auto submit_epoch = m_trace.job_at(job_no).get_submit_time();
    if (submit_epoch != Job_Record::unscheduled_sentinel()) {
      const sim_time_t submit = convert_epoch<sim_time_t>(submit_epoch);
      if (submit >= sim_start_time) {
        submit_job(job_no, submit);
      }
    }
  }

  if (m_pre_start_jobs != 0) {
    if (m_custom_scheduler != nullptr) {
      advance_to_impl<true, true>(run_limit, m_custom_scheduler);
    } else {
      advance_to_impl<false, true>(run_limit, nullptr);
    }
  }

  // The ordinary stage has no historical-job test in its compiled loop. If
  // the configured horizon ended during warm-up, the current time is already
  // at run_limit and this call is an inexpensive no-op.
  advance_to(run_limit);
}

template <typename TraceType>
void BasicSimulation<TraceType>::determine_one_job_run_time(Job_Record &job) {
  // Scheduler uses time_limit as the best estimator for planning (realistic
  // mode). run_time_mode controls how the job's actual execution length is
  // determined.

  tdiff_t run_time;

  switch (m_params.m_run_time_mode) {
  case RunTimeMode::ACTUAL:
    // Read actual_run_time from trace (most realistic)
    run_time = job.get_actual_run_time();
    break;

  case RunTimeMode::DISTRIBUTION:
    // Sample from distribution (realistic with variation)
    run_time =
        sample_run_time(job.get_limit_time(), m_params.m_run_time_distribution,
                        m_params.m_run_time_scale, m_params.m_run_time_stddev);
    job.set_actual_run_time(run_time);
    break;

  case RunTimeMode::LIMIT:
    // Use time_limit in place of run_time (unrealistic, for debugging/testing)
    run_time = job.get_limit_time();
    job.set_actual_run_time(run_time);
    break;
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::determine_job_run_time() {
  for (auto &job : m_trace.data()) {
    determine_one_job_run_time(job);
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::determine_job_run_time(
    const std::vector<job_no_t> &job_nos) {
  for (job_no_t job_no : job_nos) {
    determine_one_job_run_time(m_trace.job_at(job_no));
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::run_progressive() {
  // Minimal reset, equivalent to initialize_trace()'s own tail - no
  // load_data() call here, since there's no single file to load
  // upfront; each file gets loaded as the driving loop below reaches it.
  if (!m_checkpoint_loaded) {
    m_trace.data().clear();
    m_current_time = 0.0;
    reset_capacity_schedule();
    m_jobs_submitted = 0;
    m_jobs_started = 0;
    m_latest_submit_time = -std::numeric_limits<sim_time_t>::infinity();
    m_jobs_completed = 0;
    m_next_progressive_file = 0;
    m_last_automatic_checkpoint_jobs = 0;
    m_has_automatic_checkpoint = false;
    m_last_automatic_checkpoint_time = 0.0;
    m_pre_start_jobs = 0;
    m_warm_resource_area = 0.0;
    m_warm_resource_end = 0.0;
    if (m_custom_scheduler != nullptr) {
      m_custom_scheduler->reset_resource_accounting();
    }
  }

  // Same reasoning as run()'s single-file path: open output files
  // early so reclaiming during the run (which starts happening
  // between files here, unlike single-file batch mode) can flush to
  // them incrementally, not only at the very end.
  m_trace.set_resource_history_capacity(m_params.m_resource_history_capacity);
  m_trace.start_simulated_trace(m_params.get_outfile(), m_params.m_msec_output,
                                m_params.m_redis_uri,
                                m_params.m_redis_key_prefix);
  m_trace.start_resource_trace(m_params.get_resource_trace(),
                               m_params.m_total_nodes, m_params.m_msec_output);

  for (size_t file_index = m_next_progressive_file;
       file_index < m_params.m_infile_list_parsed.size(); ++file_index) {
    const std::string &fname = m_params.m_infile_list_parsed[file_index];
    if (m_params.m_verbose) {
      std::cout << "Loading " + fname + "...\n";
    }

    // reclaim + grow-or-abort against (what's left unreclaimed) +
    // (this file's job count) - Trace::ensure_batch_capacity(),
    // shared with append_jobs(). Also checks this file's own rows
    // are submit_time-sorted, and that its earliest submit_time
    // isn't before the previous file's latest - see its own doc
    // comment.
    auto job_nos = m_trace.load_next_file(m_current_time, fname);
    if (job_nos.empty()) {
      m_next_progressive_file = file_index + 1;
      maybe_save_automatic_checkpoint(true);
      continue; // an empty file contributes nothing to submit
    }

    // Simulation-mode-only, same condition initialize_trace() uses
    // for the single-file path - REPLAY is rejected before
    // run_progressive() is ever called (see run()), so this is
    // always true here, but kept explicit rather than assumed.
    if (m_trace.dcols().get_trace_mode() == TraceMode::SIMULATION) {
      determine_job_run_time(job_nos);
    }

    // Submit this file's jobs one at a time, in submit_time order
    // (guaranteed by load_next_file(), since m_data stays
    // submit-time sorted) - not all upfront the way single-file
    // batch mode does, since nothing becomes reclaimable until
    // advance_to() actually processes a completion, and upfront
    // submission would mean every file's jobs are already known to
    // m_data before that ever gets a chance to run once.
    for (job_no_t job_no : job_nos) {
      const auto &job = m_trace.job_at(job_no);
      sim_time_t submit_time = convert_epoch<sim_time_t>(job.get_submit_time());
      submit_job(job_no, submit_time);
    }

    // The file is now fully admitted. A periodic checkpoint taken while the
    // following advance processes completions can therefore resume directly
    // with the next file.
    m_next_progressive_file = file_index + 1;

    // This file's last job was just added to the wait queue -
    // nothing more from it to submit, so this is the point to let
    // time (and reclaiming) actually progress before the next file
    // loads. advance_to() itself is completely unmodified - called
    // exactly as it always has been, just with this file's own
    // last submit_time as the target instead of infinity.
    sim_time_t last_submit_time = convert_epoch<sim_time_t>(
        m_trace.job_at(job_nos.back()).get_submit_time());
    const sim_time_t run_limit =
        m_params.m_is_time_set ? m_params.m_max_time : last_submit_time;
    advance_to(std::min(last_submit_time, run_limit));
    maybe_save_automatic_checkpoint(true);
    if (m_params.m_is_time_set && last_submit_time >= m_params.m_max_time) {
      break;
    }
  }

  // Drain whatever is still running, or stop at the configured inclusive
  // time boundary.
  const sim_time_t run_limit = m_params.m_is_time_set
                                   ? m_params.m_max_time
                                   : std::numeric_limits<sim_time_t>::max();
  if (m_current_time < run_limit) {
    advance_to(run_limit);
  }
  m_checkpoint_loaded = false;
}

template <typename TraceType>
void BasicSimulation<TraceType>::maybe_save_automatic_checkpoint(
    bool file_boundary) {
  if (m_params.m_checkpoint_file.empty()) {
    return;
  }
#if defined(DR_EVT_HAS_SER20)
  const bool interval_due =
      m_params.m_checkpoint_interval_jobs != 0 &&
      m_jobs_completed - m_last_automatic_checkpoint_jobs >=
          m_params.m_checkpoint_interval_jobs;
  if (!file_boundary && !interval_due) {
    return;
  }
  if (file_boundary && m_has_automatic_checkpoint &&
      m_jobs_completed == m_last_automatic_checkpoint_jobs &&
      m_current_time == m_last_automatic_checkpoint_time) {
    return;
  }
  m_last_automatic_checkpoint_jobs = m_jobs_completed;
  m_has_automatic_checkpoint = true;
  m_last_automatic_checkpoint_time = m_current_time;
  save_checkpoint(m_params.m_checkpoint_file);
#else
  (void)file_boundary;
  throw std::logic_error(
      "automatic checkpoints require DR_EVT_WITH_SER20=ON");
#endif
}

template <typename TraceType>
tdiff_t BasicSimulation<TraceType>::sample_run_time(tdiff_t time_limit,
                                                    DistributionType dist,
                                                    double scale,
                                                    double stddev) {
  if (time_limit <= 0.0) {
    return 0.0;
  }

  // Lower bound for a sampled run time. A job must run for a positive
  // duration: Trace::insert_job() treats a stored actual_run_time <= 0 as
  // "not set" and substitutes the full time_limit, so clamping a low-tail
  // sample to 0 used to map it to the opposite extreme (about 16% of jobs
  // ran their whole limit with scale = stddev = 0.5). One second is the
  // trace format's own resolution.
  const double min_run_time = std::min(1.0, time_limit);

  switch (dist) {
  case DistributionType::NORMAL: {
    double mean = time_limit * scale;
    double sd = time_limit * stddev;
    std::normal_distribution<double> normal_dist(mean, sd);
    double run_time = m_rng.sample(normal_dist);
    // A real HPC scheduler kills a job at its stated time_limit -
    // it can never actually run longer than that. Cap here so a
    // wide-tailed sample can't silently let a job run past its own
    // limit, which would diverge from real system behavior.
    return std::min(time_limit, std::max(min_run_time, run_time));
  }

  case DistributionType::LOGNORMAL: {
    double mu = std::log(time_limit * scale);
    double sigma = stddev;
    std::lognormal_distribution<double> lognormal_dist(mu, sigma);
    // Positive by construction but can underflow toward 0; apply the
    // same floor, and cap at time_limit for the same reason as NORMAL.
    return std::min(time_limit,
                    std::max(min_run_time, m_rng.sample(lognormal_dist)));
  }

  case DistributionType::UNIFORM: {
    double min_run_time = time_limit * scale;
    double max_run_time = time_limit * (scale + stddev);
    std::uniform_real_distribution<double> uniform_dist(min_run_time,
                                                        max_run_time);
    // Not capped: unlike NORMAL/LOGNORMAL's unbounded-above tails,
    // this distribution's upper bound is already an explicit,
    // direct function of the caller's own scale/stddev choice -
    // exceeding time_limit here only happens if the caller
    // deliberately set scale + stddev > 1.0.
    return std::max(min_run_time, m_rng.sample(uniform_dist));
  }

  default:
    return time_limit;
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::write_simulated_trace() {
  const sim_time_t completed_through =
      m_params.m_is_time_set ? m_params.m_max_time
                             : std::numeric_limits<sim_time_t>::max();
  m_trace.start_simulated_trace(m_params.get_outfile(), m_params.m_msec_output,
                                m_params.m_redis_uri,
                                m_params.m_redis_key_prefix);
  m_trace.write_simulated_trace(m_params.get_outfile(), m_params.m_msec_output,
                                completed_through);
  if (m_params.m_verbose && !m_params.get_outfile().empty()) {
    if (m_params.m_redis_uri.empty()) {
      std::cout << "Simulated trace written to: " << m_params.get_outfile()
                << std::endl;
    } else {
      std::cout << "Simulated trace written to Redis prefix: "
                << m_params.m_redis_key_prefix << std::endl;
    }
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::flush_completed_jobs() {
  m_trace.flush_completed_jobs(m_current_time);
}

#if defined(DR_EVT_HAS_SER20)
template <typename TraceType>
void BasicSimulation<TraceType>::save_checkpoint(std::ostream &output) {
  if constexpr (std::is_same_v<TraceType, Trace> ||
                std::is_same_v<TraceType, PconTrace> ||
                std::is_same_v<TraceType, SwfTrace>) {
    if (m_custom_scheduler != nullptr &&
        typeid(*m_custom_scheduler) != typeid(CustomFCFSScheduler)) {
      throw std::logic_error(
          "checkpointing is not supported for custom scheduler subclasses");
    }
    if (m_pre_start_jobs != 0) {
      throw std::logic_error(
          "checkpointing is not supported during warm-start replay");
    }
    if (m_trace.dcols().get_trace_mode() != TraceMode::SIMULATION) {
      throw std::logic_error("checkpointing requires simulation-mode input");
    }
    const bool redis_output = !m_params.m_redis_uri.empty();
    bool simulated_output_open = m_trace.m_simulated_trace_ofs.is_open();
    bool resource_output_open = m_trace.m_resource_trace_ofs.is_open();
#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
    simulated_output_open =
        simulated_output_open || m_trace.m_redis_output != nullptr;
    resource_output_open =
        resource_output_open ||
        (m_trace.m_redis_output != nullptr &&
         m_trace.m_redis_output->resource_trace_active());
#endif
    m_trace.flush_simulated_trace_buffer(true);
    if (resource_output_open) {
      m_trace.flush_resource_history();
      m_trace.m_resource_trace_ofs.flush();
    }
    const std::uint64_t simulated_output_bytes =
        simulated_output_open && !redis_output
            ? checkpoint_output_bytes(m_trace.m_simulated_trace_ofs,
                                      "simulated trace output")
            : 0;
    const std::uint64_t resource_output_bytes =
        resource_output_open && !redis_output
            ? checkpoint_output_bytes(m_trace.m_resource_trace_ofs,
                                      "resource trace output")
            : 0;
    CheckpointRedisBoundary redis_boundary;
#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
    if (redis_output && m_trace.m_redis_output != nullptr) {
      auto captured = m_trace.m_redis_output->checkpoint_boundary();
      redis_boundary.job_csv_bytes = captured.job_csv_bytes;
      redis_boundary.resource_csv_bytes = captured.resource_csv_bytes;
      redis_boundary.resource_count = captured.resource_count;
      redis_boundary.job_ids = std::move(captured.job_ids);
      redis_boundary.resource_trace_active = captured.resource_trace_active;
    }
#else
    if (redis_output) {
      throw std::logic_error("Redis checkpoint requires Redis support");
    }
#endif

    CheckpointWriter writer(output);
    writer.bytes(checkpoint_magic.data(), checkpoint_magic.size());
    writer.value(checkpoint_version);
    writer.value(checkpoint_trace_kind<TraceType>());
    writer.value(static_cast<std::uint32_t>(
        sizeof(typename TraceType::policy_type::record_type)));

    // Configuration remains owned by Sim_Params. Persist the fields that
    // determine scheduling or output identity so a mismatched destination is
    // rejected before any state is replaced.
    writer.value(m_params.m_total_nodes);
    writer.enumeration(m_params.m_backfill_policy);
    writer.enumeration(m_params.m_priority_policy);
    writer.enumeration(m_params.m_queue_impl);
    writer.value(m_params.m_block_size);
    writer.value(m_params.m_num_max_candidates);
    writer.value(m_params.m_wait_queue_capacity);
    writer.enumeration(m_params.m_wait_queue_overflow);
    writer.enumeration(m_params.m_run_time_mode);
    writer.enumeration(m_params.m_run_time_distribution);
    writer.value(m_params.m_run_time_scale);
    writer.value(m_params.m_run_time_stddev);
    writer.string(m_params.get_outfile());
    writer.string(m_params.get_resource_trace());
    writer.string(m_params.m_redis_uri);
    writer.string(m_params.m_redis_key_prefix);
    writer.value(m_params.m_msec_output);
    writer.value(m_params.m_checkpoint_interval_jobs);
    writer.value(m_params.m_cores_per_node);
    writer.value(static_cast<std::uint64_t>(
        m_params.m_infile_list_parsed.size()));
    for (const auto &filename : m_params.m_infile_list_parsed) {
      writer.string(filename);
    }
    writer.value(m_custom_scheduler != nullptr);
    writer.value(static_cast<std::uint64_t>(m_capacity_changes.size()));
    for (const auto &change : m_capacity_changes) {
      writer.value(change.time);
      writer.value(change.total_nodes);
    }

    writer.value(m_current_time);
    writer.value(m_job_rejection_capacity);
    writer.value(m_next_capacity_change);
    writer.value(m_current_capacity);
    writer.value(m_capacity_area);
    writer.value(m_capacity_area_time);
    writer.value(m_jobs_completed);
    writer.value(m_jobs_submitted);
    writer.value(m_jobs_started);
    writer.value(m_latest_submit_time);
    writer.value(static_cast<std::uint64_t>(m_next_progressive_file));
    writer.value(m_last_automatic_checkpoint_jobs);
    writer.value(m_has_automatic_checkpoint);
    writer.value(m_last_automatic_checkpoint_time);
    writer.value(m_pre_start_jobs);
    writer.value(m_warm_resource_area);
    writer.value(m_warm_resource_end);
    writer.value(m_queue_length_sum);
    writer.value(m_queue_length_samples);
    writer.value(m_queue_length_peak);
    writer.value(m_first_appended_job_idx);
    writer.value(
        static_cast<std::uint64_t>(m_appended_job_query_records.size()));
    for (const auto &record : m_appended_job_query_records) {
      writer.value(record.submit_time);
      writer.value(record.limit_time);
      writer.value(record.num_nodes);
      writer.value(record.start_time);
      writer.value(record.end_time);
      writer.value(record.tracked);
      writer.value(record.scheduled);
      writer.value(record.rejected);
    }
    writer.value(m_scheduler->m_fcfs_reservation_time);
    auto pending_job_ids = m_scheduler->pending_job_ids();
    std::sort(pending_job_ids.begin(), pending_job_ids.end());
    writer.value(static_cast<std::uint64_t>(pending_job_ids.size()));
    for (const auto job_id : pending_job_ids) {
      writer.value(job_id);
    }
    if (m_custom_scheduler != nullptr) {
      writer.value(static_cast<std::uint64_t>(
          m_custom_scheduler->m_wait_queue.capacity()));
      writer.value(
          static_cast<std::uint64_t>(m_custom_scheduler->m_wait_queue.size()));
      for (const auto &job : m_custom_scheduler->m_wait_queue) {
        writer.value(job.job_id);
        writer.value(job.submit_time);
        writer.value(job.run_time_estimate);
        writer.value(job.nodes_requested);
        writer.value(job.m_cost);
        writer.value(job.removed);
      }
      writer.enumeration(m_custom_scheduler->m_overflow_policy);
      writer.value(m_custom_scheduler->m_eligible_end_idx);
      writer.value(m_custom_scheduler->m_current_tracked_time);
      writer.value(m_custom_scheduler->m_removed_count);
      writer.value(m_custom_scheduler->m_num_max_candidates);
      writer.value(m_custom_scheduler->m_newly_eligible_begin_idx);
      writer.value(m_custom_scheduler->m_arrival_update_pending);
      writer.value(m_custom_scheduler->m_reevaluate_all_candidates);
      writer.value(m_custom_scheduler->m_candidate_preparation_pending);
      writer.value(m_custom_scheduler->m_resource_area);
      writer.value(m_custom_scheduler->m_resource_area_time);
      writer.value(m_custom_scheduler->m_resource_area_start);
      writer.value(m_custom_scheduler->m_accounted_available_nodes);
    }

    writer.value(static_cast<std::uint64_t>(m_running_jobs.size()));
    for (const auto &[job_id, running] : m_running_jobs) {
      writer.value(job_id);
      writer.value(running.start_time);
      writer.value(running.run_time);
      writer.value(running.nodes);
    }
    writer.value(static_cast<std::uint64_t>(m_pending_queue_arrivals.size()));
    for (const auto &[time, count] : m_pending_queue_arrivals) {
      writer.value(time);
      writer.value(count);
    }
    writer.value(static_cast<std::uint64_t>(m_event_queue.size()));
    for (const auto &event : m_event_queue) {
      writer.value(event.get_job_idx());
      write_epoch(writer, event.get_time());
      writer.value(event.get_type());
    }

    writer.object(m_rng);

    writer.value(m_trace.m_num_reclaimed);
    writer.value(m_trace.m_job_store_capacity);
    writer.value(m_trace.m_job_store_capacity_resolved);
    writer.enumeration(m_trace.m_job_store_overflow);
    writer.value(m_trace.m_job_flush_interval);
    writer.value(m_trace.m_departures_since_job_flush);
    writer.value(m_trace.m_replay_jobs_enqueued);
    writer.value(m_trace.m_memory_pressure_fraction);
    writer.value(m_trace.m_has_loaded_a_file);
    write_epoch(writer, m_trace.m_last_loaded_submit_time);
    writer.value(m_trace.m_completed_count);
    writer.value(m_trace.m_wait_time_sum);
    writer.value(m_trace.m_turnaround_time_sum);
    writer.value(m_trace.m_makespan);
    writer.value(m_trace.m_next_job_to_write);

    writer.value(static_cast<std::uint64_t>(m_trace.m_data.capacity()));
    writer.value(static_cast<std::uint64_t>(m_trace.m_data.size()));
    for (const auto &job : m_trace.m_data) {
      write_epoch(writer, job.m_t_begin);
      write_epoch(writer, job.m_t_end);
      write_epoch(writer, job.m_t_submit);
      writer.value(job.m_t_limit);
      writer.value(job.m_actual_run_time);
      writer.value(job.m_num_nodes);
      writer.enumeration(job.m_q);
      writer.value(job.m_is_simulated);
#if SHOW_ORG_NO
      writer.value(job.m_org_no);
#endif
#if MARK_DAT_PERIOD
      writer.value(job.m_dat);
#endif
      writer.value(job.m_busy_nodes);
      if constexpr (std::is_same_v<TraceType, PconTrace>) {
        writer.value(job.pcon().avgpcon);
        writer.value(job.pcon().minpcon);
        writer.value(job.pcon().maxpcon);
      } else if constexpr (std::is_same_v<TraceType, SwfTrace>) {
        writer.value(job.swf_job_number());
        writer.value(job.swf_queue());
      }
    }

#if MARK_DAT_PERIOD
    writer.value(m_trace.m_ctx.m_pAll_cnt);
    write_epoch(writer, m_trace.m_ctx.m_dat_start);
    write_epoch(writer, m_trace.m_ctx.m_dat_end);
    writer.value(m_trace.m_ctx.m_dat_span);
    writer.enumeration(m_trace.m_ctx.m_prev_job_q);
    writer.value(static_cast<std::uint64_t>(m_trace.m_reserved.size()));
    for (const auto &period : m_trace.m_reserved) {
      write_epoch(writer, period.first);
      write_epoch(writer, period.second);
    }
#endif
    writer.value(m_trace.m_ctx.m_n_nodes_in_use);
    writer.value(static_cast<std::uint64_t>(m_trace.m_ctx.m_evtq.size()));
    for (const auto &event : m_trace.m_ctx.m_evtq) {
      writer.value(event.get_job_idx());
      write_epoch(writer, event.get_time());
      writer.value(event.get_type());
    }

    writer.value(m_trace.m_resource_history_capacity);
    writer.value(m_trace.m_resource_history_capacity_resolved);
    writer.value(static_cast<std::uint64_t>(
        m_trace.m_ctx.m_resource_history.capacity()));
    writer.value(
        static_cast<std::uint64_t>(m_trace.m_ctx.m_resource_history.size()));
    for (const auto &sample : m_trace.m_ctx.m_resource_history) {
      write_epoch(writer, sample.time);
      writer.value(sample.allocated);
      writer.value(sample.capacity);
      if constexpr (std::is_same_v<TraceType, PconTrace>) {
        writer.value(sample.pcon.avgpcon);
        writer.value(sample.pcon.minpcon);
        writer.value(sample.pcon.maxpcon);
      }
    }
    writer.value(m_trace.m_resource_trace_total_nodes);
    writer.value(m_trace.m_resource_trace_current_capacity);
    writer.value(m_trace.m_resource_capacity_initialized);
    writer.value(m_trace.m_resource_recording_start);
    write_epoch(writer, m_trace.m_resource_recording_baseline.time);
    writer.value(m_trace.m_resource_recording_baseline.allocated);
    writer.value(m_trace.m_resource_recording_baseline.capacity);
    if constexpr (std::is_same_v<TraceType, PconTrace>) {
      writer.value(m_trace.m_resource_recording_baseline.pcon.avgpcon);
      writer.value(m_trace.m_resource_recording_baseline.pcon.minpcon);
      writer.value(m_trace.m_resource_recording_baseline.pcon.maxpcon);
    }
    writer.value(m_trace.m_has_resource_recording_baseline);
    writer.value(m_trace.m_resource_trace_msec);
    writer.value(m_trace.m_simulated_trace_msec);
    writer.value(simulated_output_open);
    writer.value(resource_output_open);
    writer.value(simulated_output_bytes);
    writer.value(resource_output_bytes);
    writer.value(redis_output);
    writer.value(redis_boundary.job_csv_bytes);
    writer.value(redis_boundary.resource_csv_bytes);
    writer.value(redis_boundary.resource_count);
    writer.value(redis_boundary.resource_trace_active);
    writer.value(static_cast<std::uint64_t>(redis_boundary.job_ids.size()));
    for (const auto &id : redis_boundary.job_ids) {
      writer.string(id);
    }
  } else {
    static_assert(std::is_same_v<TraceType, Trace> ||
                      std::is_same_v<TraceType, PconTrace> ||
                      std::is_same_v<TraceType, SwfTrace>,
                  "checkpoint serialization is not defined for this trace");
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::save_checkpoint(const std::string &filename) {
  std::ostringstream buffer(std::ios::out | std::ios::binary);
  save_checkpoint(buffer);
  std::ofstream output(filename, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot open checkpoint for writing: " + filename);
  }
  const std::string state = buffer.str();
  output.write(state.data(), static_cast<std::streamsize>(state.size()));
  output.close();
  if (!output) {
    throw std::runtime_error("failed to finish checkpoint: " + filename);
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::load_checkpoint(std::istream &input) {
  if constexpr (std::is_same_v<TraceType, Trace> ||
                std::is_same_v<TraceType, PconTrace> ||
                std::is_same_v<TraceType, SwfTrace>) {
    if (m_custom_scheduler != nullptr &&
        typeid(*m_custom_scheduler) != typeid(CustomFCFSScheduler)) {
      throw std::logic_error(
          "checkpointing is not supported for custom scheduler subclasses");
    }

    CheckpointReader reader(input);
    std::array<char, checkpoint_magic.size()> magic{};
    reader.bytes(magic.data(), magic.size());
    if (magic != checkpoint_magic) {
      throw std::runtime_error("not a DR_EVT simulation checkpoint");
    }
    require_checkpoint_match(reader.value<std::uint32_t>(), checkpoint_version,
                             "format version");
    require_checkpoint_match(reader.value<std::uint8_t>(),
                             checkpoint_trace_kind<TraceType>(),
                             "trace type");
    require_checkpoint_match(reader.value<std::uint32_t>(),
                             static_cast<std::uint32_t>(sizeof(
                                 typename TraceType::policy_type::record_type)),
                             "trace record ABI");
    require_checkpoint_match(reader.value<num_nodes_t>(),
                             m_params.m_total_nodes, "total_nodes");
    require_checkpoint_match(reader.enumeration<BackfillPolicy>(),
                             m_params.m_backfill_policy, "backfill_policy");
    require_checkpoint_match(reader.enumeration<PriorityPolicy>(),
                             m_params.m_priority_policy, "priority_policy");
    require_checkpoint_match(reader.enumeration<QueueImplementation>(),
                             m_params.m_queue_impl, "queue_impl");
    require_checkpoint_match(reader.value<size_t>(), m_params.m_block_size,
                             "block_size");
    require_checkpoint_match(reader.value<size_t>(),
                             m_params.m_num_max_candidates,
                             "num_max_candidates");
    require_checkpoint_match(reader.value<size_t>(),
                             m_params.m_wait_queue_capacity,
                             "wait_queue_capacity");
    require_checkpoint_match(reader.enumeration<CircularOverflowPolicy>(),
                             m_params.m_wait_queue_overflow,
                             "wait_queue_overflow");
    require_checkpoint_match(reader.enumeration<RunTimeMode>(),
                             m_params.m_run_time_mode, "run_time_mode");
    require_checkpoint_match(reader.enumeration<DistributionType>(),
                             m_params.m_run_time_distribution,
                             "run_time_distribution");
    require_checkpoint_match(reader.value<double>(), m_params.m_run_time_scale,
                             "run_time_scale");
    require_checkpoint_match(reader.value<double>(), m_params.m_run_time_stddev,
                             "run_time_stddev");
    require_checkpoint_match(reader.string(), m_params.get_outfile(),
                             "simulated trace output");
    require_checkpoint_match(reader.string(), m_params.get_resource_trace(),
                             "resource trace output");
    require_checkpoint_match(reader.string(), m_params.m_redis_uri,
                             "redis_uri");
    require_checkpoint_match(reader.string(), m_params.m_redis_key_prefix,
                             "redis_key_prefix");
    require_checkpoint_match(reader.value<bool>(), m_params.m_msec_output,
                             "msec_output");
    require_checkpoint_match(reader.value<num_jobs_t>(),
                             m_params.m_checkpoint_interval_jobs,
                             "checkpoint_interval_jobs");
    require_checkpoint_match(reader.value<unsigned>(),
                             m_params.m_cores_per_node,
                             "SWF cores per node");
    const size_t progressive_file_count = reader.count();
    require_checkpoint_match(progressive_file_count,
                             m_params.m_infile_list_parsed.size(),
                             "progressive input file count");
    for (const auto &expected : m_params.m_infile_list_parsed) {
      require_checkpoint_match(reader.string(), expected,
                               "progressive input file");
    }
    const bool checkpoint_uses_custom_scheduler = reader.value<bool>();
    require_checkpoint_match(checkpoint_uses_custom_scheduler,
                             m_custom_scheduler != nullptr,
                             "custom scheduler kind");
    const size_t capacity_change_count = reader.count();
    require_checkpoint_match(capacity_change_count, m_capacity_changes.size(),
                             "capacity schedule size");
    for (const auto &expected : m_capacity_changes) {
      require_checkpoint_match(reader.value<sim_time_t>(), expected.time,
                               "capacity schedule time");
      require_checkpoint_match(reader.value<num_nodes_t>(),
                               expected.total_nodes, "capacity schedule nodes");
    }

    m_trace.m_simulated_trace_ofs.close();
    m_trace.m_resource_trace_ofs.close();

    m_current_time = reader.value<sim_time_t>();
    m_job_rejection_capacity = reader.value<num_nodes_t>();
    m_next_capacity_change = reader.value<size_t>();
    m_current_capacity = reader.value<num_nodes_t>();
    m_capacity_area = reader.value<tdiff_t>();
    m_capacity_area_time = reader.value<sim_time_t>();
    m_jobs_completed = reader.value<num_jobs_t>();
    m_jobs_submitted = reader.value<num_jobs_t>();
    m_jobs_started = reader.value<num_jobs_t>();
    m_latest_submit_time = reader.value<sim_time_t>();
    m_next_progressive_file = reader.count();
    if (m_next_progressive_file > m_params.m_infile_list_parsed.size()) {
      throw std::runtime_error(
          "checkpoint progressive input cursor is out of range");
    }
    m_last_automatic_checkpoint_jobs = reader.value<num_jobs_t>();
    m_has_automatic_checkpoint = reader.value<bool>();
    m_last_automatic_checkpoint_time = reader.value<sim_time_t>();
    m_checkpoint_loaded = true;
    m_pre_start_jobs = reader.value<size_t>();
    m_warm_resource_area = reader.value<tdiff_t>();
    m_warm_resource_end = reader.value<sim_time_t>();
    m_queue_length_sum = reader.value<size_t>();
    m_queue_length_samples = reader.value<size_t>();
    m_queue_length_peak = reader.value<size_t>();
    m_first_appended_job_idx = reader.value<job_no_t>();
    m_appended_job_query_records.clear();
    m_appended_job_query_records.resize(reader.count());
    for (auto &record : m_appended_job_query_records) {
      record.submit_time = reader.value<sim_time_t>();
      record.limit_time = reader.value<tdiff_t>();
      record.num_nodes = reader.value<num_nodes_t>();
      record.start_time = reader.value<sim_time_t>();
      record.end_time = reader.value<sim_time_t>();
      record.tracked = reader.value<bool>();
      record.scheduled = reader.value<bool>();
      record.rejected = reader.value<bool>();
    }
    const sim_time_t reservation_time = reader.value<sim_time_t>();
    std::vector<job_no_t> pending_job_ids;
    const size_t pending_job_count = reader.count();
    pending_job_ids.reserve(pending_job_count);
    for (size_t i = 0; i < pending_job_count; ++i) {
      pending_job_ids.push_back(reader.value<job_no_t>());
    }
    std::sort(pending_job_ids.begin(), pending_job_ids.end());
    if (std::adjacent_find(pending_job_ids.begin(), pending_job_ids.end()) !=
        pending_job_ids.end()) {
      throw std::runtime_error(
          "checkpoint scheduler contains a duplicate job identifier");
    }
    if (checkpoint_uses_custom_scheduler) {
      const size_t queue_capacity = reader.count();
      const size_t queue_size = reader.count();
      if (queue_size > queue_capacity) {
        throw std::runtime_error(
            "checkpoint custom scheduler queue exceeds its capacity");
      }
      m_custom_scheduler->m_wait_queue.clear();
      m_custom_scheduler->m_wait_queue.set_capacity(queue_capacity);
      for (size_t i = 0; i < queue_size; ++i) {
        const auto job_id = reader.value<job_no_t>();
        const auto submit_time = reader.value<sim_time_t>();
        const auto run_time = reader.value<tdiff_t>();
        const auto nodes = reader.value<num_nodes_t>();
        const auto cost = reader.value<job_cost_t>();
        const auto removed = reader.value<bool>();
        m_custom_scheduler->m_wait_queue.push_back(
            CustomFCFSScheduler::JobEntry(job_id, submit_time, run_time, nodes,
                                          cost));
        m_custom_scheduler->m_wait_queue.back().removed = removed;
      }
      const auto overflow_policy = reader.enumeration<CircularOverflowPolicy>();
      require_checkpoint_match(overflow_policy, m_params.m_wait_queue_overflow,
                               "custom scheduler overflow policy");
      m_custom_scheduler->m_overflow_policy = overflow_policy;
      m_custom_scheduler->m_eligible_end_idx = reader.value<size_t>();
      m_custom_scheduler->m_current_tracked_time = reader.value<sim_time_t>();
      m_custom_scheduler->m_removed_count = reader.value<size_t>();
      const size_t num_max_candidates = reader.value<size_t>();
      require_checkpoint_match(num_max_candidates,
                               m_params.m_num_max_candidates,
                               "custom scheduler candidate limit");
      m_custom_scheduler->m_num_max_candidates = num_max_candidates;
      m_custom_scheduler->m_newly_eligible_begin_idx = reader.value<size_t>();
      m_custom_scheduler->m_arrival_update_pending = reader.value<bool>();
      m_custom_scheduler->m_reevaluate_all_candidates = reader.value<bool>();
      m_custom_scheduler->m_candidate_preparation_pending =
          reader.value<bool>();
      m_custom_scheduler->m_resource_area = reader.value<tdiff_t>();
      m_custom_scheduler->m_resource_area_time = reader.value<sim_time_t>();
      m_custom_scheduler->m_resource_area_start = reader.value<sim_time_t>();
      m_custom_scheduler->m_accounted_available_nodes =
          reader.value<num_nodes_t>();
      if (m_custom_scheduler->m_eligible_end_idx > queue_size ||
          m_custom_scheduler->m_newly_eligible_begin_idx >
              m_custom_scheduler->m_eligible_end_idx ||
          m_custom_scheduler->m_removed_count >
              m_custom_scheduler->m_eligible_end_idx ||
          m_custom_scheduler->m_accounted_available_nodes >
              m_params.m_total_nodes) {
        throw std::runtime_error(
            "checkpoint custom scheduler indices are inconsistent");
      }
    }

    m_running_jobs.clear();
    for (size_t i = 0, count = reader.count(); i < count; ++i) {
      const auto job_id = reader.value<job_no_t>();
      Running_Job running{reader.value<sim_time_t>(), reader.value<tdiff_t>(),
                          reader.value<num_nodes_t>()};
      m_running_jobs.emplace(job_id, running);
    }
    m_pending_queue_arrivals.clear();
    for (size_t i = 0, count = reader.count(); i < count; ++i) {
      const auto time = reader.value<sim_time_t>();
      const auto count_at_time = reader.value<size_t>();
      m_pending_queue_arrivals.emplace(time, count_at_time);
    }
    m_event_queue.clear();
    for (size_t i = 0, count = reader.count(); i < count; ++i) {
      const auto job_id = reader.value<job_no_t>();
      const auto time = read_epoch(reader);
      const auto type = reader.value<bool>();
      m_event_queue.emplace(job_id, time, type);
    }
    reader.object(m_rng);

    m_trace.m_num_reclaimed = reader.value<size_t>();
    m_trace.m_job_store_capacity = reader.value<size_t>();
    m_trace.m_job_store_capacity_resolved = reader.value<bool>();
    m_trace.m_job_store_overflow = reader.enumeration<CircularOverflowPolicy>();
    m_trace.m_job_flush_interval = reader.value<size_t>();
    m_trace.m_departures_since_job_flush = reader.value<size_t>();
    m_trace.m_replay_jobs_enqueued = reader.value<size_t>();
    m_trace.m_memory_pressure_fraction = reader.value<double>();
    m_trace.m_has_loaded_a_file = reader.value<bool>();
    m_trace.m_last_loaded_submit_time = read_epoch(reader);
    m_trace.m_completed_count = reader.value<num_jobs_t>();
    m_trace.m_wait_time_sum = reader.value<tdiff_t>();
    m_trace.m_turnaround_time_sum = reader.value<tdiff_t>();
    m_trace.m_makespan = reader.value<sim_time_t>();
    m_trace.m_next_job_to_write = reader.value<size_t>();

    const size_t job_capacity = reader.count();
    const size_t job_count = reader.count();
    if (job_count > job_capacity) {
      throw std::runtime_error("checkpoint job store exceeds its capacity");
    }
    m_trace.m_data.clear();
    m_trace.m_data.set_capacity(job_capacity);
    for (size_t i = 0; i < job_count; ++i) {
      const auto begin = read_epoch(reader);
      const auto end = read_epoch(reader);
      const auto submit = read_epoch(reader);
      const auto limit = reader.value<timeout_t>();
      const auto actual = reader.value<tdiff_t>();
      const auto nodes = reader.value<num_nodes_t>();
      const auto queue = reader.enumeration<job_queue_t>();
      const auto simulated = reader.value<bool>();
      Job_Record job(submit, nodes, queue, limit);
      job.m_t_begin = begin;
      job.m_t_end = end;
      job.m_actual_run_time = actual;
      job.m_is_simulated = simulated;
#if SHOW_ORG_NO
      job.m_org_no = reader.value<job_no_t>();
#endif
#if MARK_DAT_PERIOD
      job.m_dat = reader.value<bool>();
#endif
      job.m_busy_nodes = reader.value<num_nodes_t>();
      if constexpr (std::is_same_v<TraceType, PconTrace>) {
        const Pcon_Values pcon{reader.value<double>(), reader.value<double>(),
                               reader.value<double>()};
        m_trace.m_data.push_back(Pcon_Job_Record(std::move(job), pcon));
      } else if constexpr (std::is_same_v<TraceType, SwfTrace>) {
        const auto swf_no = reader.value<std::uint32_t>();
        const auto swf_q = reader.value<std::uint8_t>();
        m_trace.m_data.push_back(Swf_Job_Record(std::move(job), swf_no, swf_q));
      } else {
        m_trace.m_data.push_back(std::move(job));
      }
    }

    const auto first_resident_job =
        static_cast<job_no_t>(m_trace.m_num_reclaimed);
    const auto resident_job_count =
        static_cast<job_no_t>(m_trace.m_data.size());
    m_trace.reset_policy_runtime_state();
    for (const auto &[job_id, running] : m_running_jobs) {
      (void)running;
      if (job_id < first_resident_job ||
          job_id - first_resident_job >= resident_job_count) {
        throw std::runtime_error(
            "checkpoint running-job map references a non-resident job");
      }
      m_trace.restore_policy_running_job(
          m_trace.m_data[job_id - first_resident_job]);
    }

#if MARK_DAT_PERIOD
    m_trace.m_ctx.m_pAll_cnt = reader.value<num_jobs_t>();
    m_trace.m_ctx.m_dat_start = read_epoch(reader);
    m_trace.m_ctx.m_dat_end = read_epoch(reader);
    m_trace.m_ctx.m_dat_span = reader.value<tdiff_t>();
    m_trace.m_ctx.m_prev_job_q = reader.enumeration<job_queue_t>();
    m_trace.m_reserved.clear();
    for (size_t i = 0, count = reader.count(); i < count; ++i) {
      const auto begin = read_epoch(reader);
      const auto end = read_epoch(reader);
      m_trace.m_reserved.emplace_back(begin, end);
    }
#endif
    m_trace.m_ctx.m_n_nodes_in_use = reader.value<num_nodes_t>();
    m_trace.m_ctx.m_evtq.clear();
    for (size_t i = 0, count = reader.count(); i < count; ++i) {
      const auto job_id = reader.value<job_no_t>();
      const auto time = read_epoch(reader);
      const auto type = reader.value<bool>();
      m_trace.m_ctx.m_evtq.emplace(job_id, time, type);
    }

    m_trace.m_resource_history_capacity = reader.value<size_t>();
    m_trace.m_resource_history_capacity_resolved = reader.value<bool>();
    const size_t resource_capacity = reader.count();
    const size_t resource_count = reader.count();
    if (resource_count > resource_capacity) {
      throw std::runtime_error(
          "checkpoint resource history exceeds its capacity");
    }
    m_trace.m_ctx.m_resource_history.clear();
    m_trace.m_ctx.m_resource_history.set_capacity(resource_capacity);
    for (size_t i = 0; i < resource_count; ++i) {
      const auto time = read_epoch(reader);
      const auto allocated = reader.value<num_nodes_t>();
      const auto capacity = reader.value<num_nodes_t>();
      if constexpr (std::is_same_v<TraceType, PconTrace>) {
        const Pcon_Values pcon{reader.value<double>(), reader.value<double>(),
                               reader.value<double>()};
        m_trace.m_ctx.m_resource_history.push_back(
            Pcon_Resource_Sample{time, allocated, capacity, pcon});
      } else {
        m_trace.m_ctx.m_resource_history.push_back(
            Standard_Resource_Sample{time, allocated, capacity});
      }
    }
    m_trace.m_resource_trace_total_nodes = reader.value<num_nodes_t>();
    m_trace.m_resource_trace_current_capacity = reader.value<num_nodes_t>();
    m_trace.m_resource_capacity_initialized = reader.value<bool>();
    m_trace.m_resource_recording_start = reader.value<sim_time_t>();
    m_trace.m_resource_recording_baseline.time = read_epoch(reader);
    m_trace.m_resource_recording_baseline.allocated =
        reader.value<num_nodes_t>();
    m_trace.m_resource_recording_baseline.capacity =
        reader.value<num_nodes_t>();
    if constexpr (std::is_same_v<TraceType, PconTrace>) {
      m_trace.m_resource_recording_baseline.pcon = {reader.value<double>(),
                                                    reader.value<double>(),
                                                    reader.value<double>()};
    }
    m_trace.m_has_resource_recording_baseline = reader.value<bool>();
    m_trace.m_resource_trace_msec = reader.value<bool>();
    m_trace.m_simulated_trace_msec = reader.value<bool>();
    const bool simulated_output_open = reader.value<bool>();
    const bool resource_output_open = reader.value<bool>();
    const auto simulated_output_bytes = reader.value<std::uint64_t>();
    const auto resource_output_bytes = reader.value<std::uint64_t>();
    const bool redis_output = reader.value<bool>();
    CheckpointRedisBoundary redis_boundary;
    redis_boundary.job_csv_bytes = reader.value<std::uint64_t>();
    redis_boundary.resource_csv_bytes = reader.value<std::uint64_t>();
    redis_boundary.resource_count = reader.value<std::uint64_t>();
    redis_boundary.resource_trace_active = reader.value<bool>();
    redis_boundary.job_ids.resize(reader.count());
    for (auto &id : redis_boundary.job_ids) {
      id = reader.string();
    }
    m_trace.m_simulated_trace_buffer.clear();

    for (const auto job_id : pending_job_ids) {
      if (job_id < first_resident_job ||
          job_id - first_resident_job >= resident_job_count) {
        throw std::runtime_error(
            "checkpoint scheduler references a non-resident job");
      }
      const auto &job = m_trace.m_data[job_id - first_resident_job];
      if (job.is_scheduled() ||
          job.get_submit_time() == Job_Record::unscheduled_sentinel()) {
        throw std::runtime_error(
            "checkpoint scheduler references a non-pending job");
      }
    }
    if (checkpoint_uses_custom_scheduler) {
      auto restored_pending_ids = m_custom_scheduler->pending_job_ids();
      std::sort(restored_pending_ids.begin(), restored_pending_ids.end());
      if (restored_pending_ids != pending_job_ids) {
        throw std::runtime_error(
            "checkpoint custom scheduler queue ownership is inconsistent");
      }
    } else {
      m_scheduler = create_scheduler(
          m_params.m_total_nodes, m_trace.m_data.size(),
          m_params.m_backfill_policy, m_params.m_priority_policy,
          m_params.m_queue_impl, m_params.m_block_size,
          m_params.m_wait_queue_capacity, m_params.m_wait_queue_overflow);
      for (const auto job_id : pending_job_ids) {
        const auto &job = m_trace.m_data[job_id - first_resident_job];
        m_scheduler->insert_job(
            job_id, convert_epoch<sim_time_t>(job.get_submit_time()),
            job.get_limit_time(), job.get_num_nodes());
      }
      m_scheduler->sync_to(m_current_time);
    }
    m_scheduler->m_fcfs_reservation_time = reservation_time;

    if (redis_output) {
#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
      m_trace.m_redis_output.reset();
      RedisCheckpointBoundary native_boundary;
      native_boundary.job_csv_bytes = redis_boundary.job_csv_bytes;
      native_boundary.resource_csv_bytes = redis_boundary.resource_csv_bytes;
      native_boundary.resource_count = redis_boundary.resource_count;
      native_boundary.job_ids = std::move(redis_boundary.job_ids);
      native_boundary.resource_trace_active =
          redis_boundary.resource_trace_active;
      RedisOutput::archive_checkpoint_namespace(
          m_params.m_redis_uri, m_params.m_redis_key_prefix, native_boundary);
      if (simulated_output_open) {
        m_trace.start_simulated_trace(
            m_params.get_outfile(), m_params.m_msec_output,
            m_params.m_redis_uri, m_params.m_redis_key_prefix);
      }
      if (resource_output_open) {
        std::string header = "time,free_nodes,allocated_nodes";
        header += TraceType::policy_type::resource_columns();
        header += '\n';
        m_trace.m_redis_output->start_resource_trace(
            header, m_params.m_msec_output);
      }
#else
      throw std::logic_error("Redis checkpoint requires Redis support");
#endif
    } else if (simulated_output_open) {
      const auto filename = m_params.get_outfile();
      start_restart_output_segment(filename, simulated_output_bytes,
                                   m_trace.m_simulated_trace_ofs);
    }
    if (!redis_output && resource_output_open) {
      const auto filename = m_params.get_resource_trace();
      start_restart_output_segment(filename, resource_output_bytes,
                                   m_trace.m_resource_trace_ofs);
    }
  } else {
    static_assert(std::is_same_v<TraceType, Trace> ||
                      std::is_same_v<TraceType, PconTrace> ||
                      std::is_same_v<TraceType, SwfTrace>,
                  "checkpoint deserialization is not defined for this trace");
  }
}

template <typename TraceType>
void BasicSimulation<TraceType>::load_checkpoint(const std::string &filename) {
  std::ifstream input(filename, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open checkpoint for reading: " + filename);
  }
  load_checkpoint(input);
}
#endif

template <typename TraceType>
void BasicSimulation<TraceType>::write_resource_trace(
    const std::string &filename) {
  if (filename.empty() && m_params.m_redis_uri.empty()) {
    return;
  }

  // A direct API caller may finalize Redis resource output without first
  // calling run() or append_job(s), so initialize the shared Redis sink here
  // too. Never reopen the ordinary job file after it has been finalized.
  if (!m_params.m_redis_uri.empty()) {
    m_trace.start_simulated_trace(m_params.get_outfile(),
                                  m_params.m_msec_output, m_params.m_redis_uri,
                                  m_params.m_redis_key_prefix);
  }
  m_trace.start_resource_trace(filename, m_params.m_total_nodes,
                               m_params.m_msec_output);

  // Trace's own context is populated identically regardless of trace
  // mode (both go through the same process_single_event()/
  // process_events_until() choke points), so this is unconditional now -
  // no more separate simulation-mode-only tracking to maintain here.
  m_trace.write_resource_trace(filename, m_params.m_total_nodes,
                               m_params.m_msec_output);
  if (m_params.m_verbose) {
    if (m_params.m_redis_uri.empty()) {
      std::cout << "Resource trace written to: " << filename << std::endl;
    } else {
      std::cout << "Resource trace written to Redis key: "
                << m_params.m_redis_key_prefix << ":resources:csv" << std::endl;
    }
  }
}

// Public API methods for online/streaming simulation mode
// Allow external code (e.g., gRPC server) to feed jobs and control simulation

template <typename TraceType>
job_no_t BasicSimulation<TraceType>::append_job(sim_time_t submit_time,
                                                num_nodes_t num_nodes,
                                                const std::string &queue,
                                                tdiff_t limit_time,
                                                std::optional<tdiff_t>
                                                    actual_run_time) {
  if (submit_time < m_current_time) {
    throw std::runtime_error(
        "Cannot append job with submit_time < current_time. "
        "submit_time=" +
        std::to_string(submit_time) +
        " but current_time=" + std::to_string(m_current_time));
  }
  if (submit_time < m_latest_submit_time) {
    // The wait queues release arrivals in insertion order, so this job
    // would silently wait until the later job already queued ahead of it
    // arrives. Reject it, matching append_jobs()' ordering requirement.
    throw std::invalid_argument(
        "Cannot append job with submit_time=" + std::to_string(submit_time) +
        " earlier than an already appended job's submit_time=" +
        std::to_string(m_latest_submit_time) +
        "; append jobs in non-decreasing submit_time order");
  }
  const timeout_t stored_limit = checked_streaming_limit(limit_time);
  if (actual_run_time &&
      (!std::isfinite(*actual_run_time) || *actual_run_time <= 0.0 ||
       *actual_run_time > static_cast<tdiff_t>(stored_limit))) {
    throw std::invalid_argument(
        "actual_run_time must be positive and no greater than limit_time");
  }

  // Online callers need not invoke run() first. Open incremental outputs before
  // this insertion can reclaim a completed record from a full job store.
  m_trace.set_job_flush_interval(m_params.m_job_flush_interval);
  m_trace.set_resource_history_capacity(m_params.m_resource_history_capacity);
  m_trace.start_simulated_trace(m_params.get_outfile(), m_params.m_msec_output,
                                m_params.m_redis_uri,
                                m_params.m_redis_key_prefix);
  m_trace.start_resource_trace(m_params.get_resource_trace(),
                               m_params.m_total_nodes, m_params.m_msec_output);

  time_t sec = static_cast<time_t>(submit_time);
  float frac = submit_time - sec;
  epoch_t submit_epoch = {sec, frac};

  job_queue_t q = QueueUnknown;
#if DR_EVT_LEGACY_QUEUE_INPUT
  set_by(q, queue);
#else
  set_by_queue_id(q, queue);
#endif

  job_no_t job_idx = m_trace.append_job(m_current_time, submit_epoch, num_nodes,
                                        q, stored_limit, actual_run_time);
  submit_job(job_idx, submit_time);
  record_appended_job(job_idx, submit_time, num_nodes, stored_limit);
  return job_idx;
}

template <typename TraceType>
std::vector<job_no_t> BasicSimulation<TraceType>::append_jobs(
    const std::vector<Job_Append_Request> &requests) {
  // Same one-time, idempotent setup as append_job(), once for this batch.
  m_trace.set_job_flush_interval(m_params.m_job_flush_interval);
  m_trace.set_resource_history_capacity(m_params.m_resource_history_capacity);
  m_trace.start_simulated_trace(m_params.get_outfile(), m_params.m_msec_output,
                                m_params.m_redis_uri,
                                m_params.m_redis_key_prefix);
  m_trace.start_resource_trace(m_params.get_resource_trace(),
                               m_params.m_total_nodes, m_params.m_msec_output);

  // Validate every request's submit_time before appending any of them
  // - same precondition append_job() enforces per-job, checked here
  // for the whole batch up front (see this function's own doc
  // comment for what "all-or-nothing" does and doesn't cover).
  for (size_t i = 0; i < requests.size(); ++i) {
    if (requests[i].submit_time < m_current_time) {
      throw std::runtime_error(
          "Cannot append job with submit_time < current_time. "
          "request " +
          std::to_string(i) +
          " has submit_time=" + std::to_string(requests[i].submit_time) +
          " but current_time=" + std::to_string(m_current_time));
    }
    if (requests[i].submit_time < m_latest_submit_time) {
      throw std::invalid_argument(
          "Cannot append request " + std::to_string(i) + " with submit_time=" +
          std::to_string(requests[i].submit_time) +
          " earlier than an already appended job's submit_time=" +
          std::to_string(m_latest_submit_time) +
          "; append jobs in non-decreasing submit_time order");
    }
    const timeout_t stored_limit =
        checked_streaming_limit(requests[i].limit_time);
    if (requests[i].actual_run_time &&
        (!std::isfinite(*requests[i].actual_run_time) ||
         *requests[i].actual_run_time <= 0.0 ||
         *requests[i].actual_run_time > static_cast<tdiff_t>(stored_limit))) {
      throw std::invalid_argument(
          "request " + std::to_string(i) +
          " actual_run_time must be positive and no greater than limit_time");
    }
  }

  std::vector<dr_evt::Job_Append_Request> trace_reqs;
  trace_reqs.reserve(requests.size());
  for (const auto &r : requests) {
    time_t sec = static_cast<time_t>(r.submit_time);
    float frac = r.submit_time - sec;
    job_queue_t q = QueueUnknown;
#if DR_EVT_LEGACY_QUEUE_INPUT
    set_by(q, r.queue);
#else
    set_by_queue_id(q, r.queue);
#endif
    trace_reqs.push_back(
        dr_evt::Job_Append_Request{epoch_t{sec, frac}, r.num_nodes, q,
                                   static_cast<timeout_t>(r.limit_time),
                                   r.actual_run_time});
  }

  std::vector<job_no_t> job_idxs =
      m_trace.append_jobs(m_current_time, trace_reqs);
  for (size_t i = 0; i < job_idxs.size(); ++i) {
    submit_job(job_idxs[i], requests[i].submit_time);
    record_appended_job(job_idxs[i], requests[i].submit_time,
                        requests[i].num_nodes,
                        static_cast<timeout_t>(requests[i].limit_time));
  }
  return job_idxs;
}

template <typename TraceType>
void BasicSimulation<TraceType>::record_appended_job(job_no_t job_idx,
                                                     sim_time_t submit_time,
                                                     num_nodes_t num_nodes,
                                                     tdiff_t limit_time) {
  if (m_appended_job_query_records.empty()) {
    m_first_appended_job_idx = job_idx;
  }
  if (job_idx < m_first_appended_job_idx) {
    throw std::logic_error("Appended job identifiers must be monotonic");
  }
  const size_t offset = job_idx - m_first_appended_job_idx;
  if (offset >= m_appended_job_query_records.size()) {
    m_appended_job_query_records.resize(offset + 1);
  }
  auto &query = m_appended_job_query_records[offset];
  query.submit_time = submit_time;
  query.limit_time = limit_time;
  query.num_nodes = num_nodes;
  query.tracked = true;
  query.rejected = m_trace.job_at(job_idx).get_submit_time() ==
                   Job_Record::unscheduled_sentinel();
}

template <typename TraceType>
void BasicSimulation<TraceType>::record_appended_job_start(job_no_t job_idx) {
  if (job_idx < m_first_appended_job_idx) {
    return;
  }
  const size_t offset = job_idx - m_first_appended_job_idx;
  if (offset >= m_appended_job_query_records.size() ||
      !m_appended_job_query_records[offset].tracked) {
    return;
  }
  const auto &job = m_trace.job_at(job_idx);
  auto &query = m_appended_job_query_records[offset];
  query.start_time = convert_epoch<sim_time_t>(job.get_begin_time());
  query.end_time = convert_epoch<sim_time_t>(job.get_end_time());
  query.scheduled = true;
}

template <typename TraceType>
std::vector<typename BasicSimulation<TraceType>::Job_Status>
BasicSimulation<TraceType>::get_job_statuses(
    const std::vector<job_no_t> &job_idxs) const {
  std::map<job_no_t, sim_time_t> expected_starts;
  std::map<sim_time_t, num_nodes_t> releases;
  num_nodes_t available = get_available_nodes();
  for (const auto &[job_idx, running] : m_running_jobs) {
    (void)job_idx;
    const sim_time_t end = running.start_time + running.run_time;
    if (end > m_current_time) {
      releases[end] += running.nodes;
    }
  }

  sim_time_t projection_time = m_current_time;
  for (const job_no_t pending_id : m_scheduler->pending_job_ids()) {
    const auto &record = m_trace.job_at(pending_id);
    const sim_time_t submit_time =
        convert_epoch<sim_time_t>(record.get_submit_time());
    projection_time = std::max(projection_time, submit_time);
    auto release = releases.begin();
    while (release != releases.end() && release->first <= projection_time) {
      available += release->second;
      release = releases.erase(release);
    }
    while (available < record.get_num_nodes() && !releases.empty()) {
      projection_time = releases.begin()->first;
      available += releases.begin()->second;
      releases.erase(releases.begin());
    }
    if (available < record.get_num_nodes()) {
      continue;
    }
    const bool is_appended =
        pending_id >= m_first_appended_job_idx &&
        pending_id - m_first_appended_job_idx <
            m_appended_job_query_records.size() &&
        m_appended_job_query_records[pending_id - m_first_appended_job_idx]
            .tracked;
    if (is_appended) {
      expected_starts[pending_id] = projection_time;
    }
    available -= record.get_num_nodes();
    releases[projection_time + record.get_limit_time()] +=
        record.get_num_nodes();
  }

  std::vector<Job_Status> result;
  result.reserve(job_idxs.size());
  for (const job_no_t job_idx : job_idxs) {
    if (job_idx < m_first_appended_job_idx ||
        job_idx - m_first_appended_job_idx >=
            m_appended_job_query_records.size() ||
        !m_appended_job_query_records[job_idx - m_first_appended_job_idx]
             .tracked) {
      throw std::out_of_range("No appended job with job_idx=" +
                              std::to_string(job_idx));
    }
    const auto &job =
        m_appended_job_query_records[job_idx - m_first_appended_job_idx];
    Job_Status status{job_idx, Job_State::PENDING, std::nullopt, std::nullopt,
                      std::nullopt};
    if (job.rejected) {
      status.state = Job_State::REJECTED;
    } else if (job.scheduled) {
      status.state = job.end_time <= m_current_time ? Job_State::COMPLETED
                                                    : Job_State::RUNNING;
      status.start_time = job.start_time;
      status.end_time = job.end_time;
    } else if (const auto it = expected_starts.find(job_idx);
               it != expected_starts.end()) {
      status.expected_start_time = it->second;
    }
    result.push_back(status);
  }
  return result;
}

template <typename TraceType>
void BasicSimulation<TraceType>::submit_job(job_no_t job_idx,
                                            sim_time_t submit_time) {
  // Validate preconditions
  if (submit_time < m_current_time) {
    throw std::runtime_error(
        "Cannot submit job with submit_time < current_time. "
        "Job " +
        std::to_string(job_idx) +
        " has submit_time=" + std::to_string(submit_time) +
        " but current_time=" + std::to_string(m_current_time));
  }

  // Submit to scheduler (scheduler maintains internal wait queue)
  // Scheduler uses time_limit as the best estimator for planning
  // job_at() below throws its own clear, correctly-bounds-checked
  // error if job_idx is invalid - accounting for m_num_reclaimed,
  // unlike a manual "job_idx >= m_trace.data().size()" check would
  // (data().size() is m_data's *current* physical count, not the
  // total job count ever seen, once anything's been reclaimed).
  auto &job = m_trace.job_at(job_idx);
  tdiff_t run_time_estimate = job.get_limit_time();
  num_nodes_t nodes = job.get_num_nodes();

  if (nodes > m_job_rejection_capacity) {
    // This job can never be scheduled, regardless of how long the
    // simulation runs - total_nodes is fixed for the whole run, so
    // no future state ever frees up enough capacity. Reject it here,
    // before it ever enters the wait queue: leaving it there would
    // silently block every job behind it in FCFS order, and its
    // begin_time/end_time would stay at unscheduled_sentinel()
    // forever - exactly the "reaches output still unresolved" state
    // that shouldn't be possible.
    //
    // Also mark submit_time as the sentinel: m_data's front-reclaim
    // sweep treats that as "skip immediately, will never resolve" -
    // otherwise a rejected job at the front would stall the sweep
    // forever, since end_time never resolves for it either.
    job.set_submit_time(Job_Record::unscheduled_sentinel());
    std::cerr << "Job " << job_idx << " rejected: requests " << nodes
              << " nodes, exceeds total_nodes (" << m_job_rejection_capacity
              << "); this job can never be scheduled." << std::endl;
    return;
  }

  // Snapshot system occupancy at the moment of arrival - same
  // statistic load_data()'s own submission loop records
  // (Job_Record::m_busy_nodes, written out as the trace's
  // "busy_nodes" column), which submit_job() would otherwise never
  // populate for a streaming-arrived job (left at the constructor's
  // default of 0, silently wrong rather than merely unset). Must run
  // before insert_job() below, so it reflects other jobs' occupancy
  // at this moment, not including this job's own allocation.
#if MARK_DAT_PERIOD
  job.set_busy_nodes(get_nodes_in_use(), m_trace.in_dat_period());
#else
  job.set_busy_nodes(get_nodes_in_use());
#endif

  m_scheduler->insert_job(job_idx, submit_time, run_time_estimate, nodes);
  ++m_pending_queue_arrivals[submit_time];
  m_latest_submit_time = std::max(m_latest_submit_time, submit_time);
  // Counted here, after the scheduler accepted it, so rejected jobs above
  // are excluded. Starts are counted separately in m_jobs_started.
  m_jobs_submitted++;
}

template <typename TraceType>
void BasicSimulation<TraceType>::record_queue_arrivals(
    sim_time_t current_time) {
  auto arrivals = m_pending_queue_arrivals.find(current_time);
  if (arrivals == m_pending_queue_arrivals.end()) {
    return;
  }

  const size_t count = arrivals->second;
  const size_t active_after_sync = m_scheduler->active_job_count();
  if (active_after_sync < count) {
    throw std::logic_error(
        "scheduler queue contains fewer jobs than arrived at current time");
  }

  size_t jobs_already_waiting = active_after_sync - count;
  for (size_t i = 0; i < count; ++i) {
    m_queue_length_sum += jobs_already_waiting + i;
    ++m_queue_length_samples;
  }
  m_pending_queue_arrivals.erase(arrivals);
}

template <typename TraceType>
void BasicSimulation<TraceType>::reset_capacity_schedule() {
  m_next_capacity_change = 0;
  m_current_capacity = m_params.m_total_nodes;
  m_trace.reset_resource_capacity(m_params.m_total_nodes);
  reset_capacity_accounting(m_current_time);
}

template <typename TraceType>
void BasicSimulation<TraceType>::reset_capacity_accounting(
    sim_time_t start_time) {
  m_capacity_area = 0.0;
  m_capacity_area_time = start_time;
}

template <typename TraceType>
void BasicSimulation<TraceType>::advance_capacity_accounting_to(
    sim_time_t current_time) {
  if (!std::isfinite(current_time)) {
    return;
  }
  if (current_time < m_capacity_area_time) {
    throw std::logic_error("capacity accounting cannot move backward in time");
  }
  const num_nodes_t effective_capacity =
      std::max(m_current_capacity, get_nodes_in_use());
  m_capacity_area += static_cast<tdiff_t>(effective_capacity) *
                     (current_time - m_capacity_area_time);
  m_capacity_area_time = current_time;
}

template <typename TraceType>
tdiff_t BasicSimulation<TraceType>::capacity_area_through(
    sim_time_t through_time) const {
  tdiff_t area = m_capacity_area;
  if (std::isfinite(through_time) && through_time > m_capacity_area_time) {
    const num_nodes_t effective_capacity =
        std::max(m_current_capacity, get_nodes_in_use());
    area += static_cast<tdiff_t>(effective_capacity) *
            (through_time - m_capacity_area_time);
  }
  return area;
}

template <typename TraceType>
bool BasicSimulation<TraceType>::apply_capacity_changes(
    sim_time_t current_time) {
  bool changed = false;
  while (m_next_capacity_change < m_capacity_changes.size() &&
         m_capacity_changes[m_next_capacity_change].time <= current_time) {
    const auto &change = m_capacity_changes[m_next_capacity_change++];
    m_current_capacity = change.total_nodes;
    m_trace.set_resource_capacity(change.time, change.total_nodes);
    changed = true;
  }
  return changed;
}

template <typename TraceType>
void BasicSimulation<TraceType>::advance_to(sim_time_t target_time) {
  if (m_custom_scheduler != nullptr) {
    advance_to_impl<true, false>(target_time, m_custom_scheduler);
  } else {
    advance_to_impl<false, false>(target_time, nullptr);
  }
}

template <typename TraceType>
template <bool AccountResources, bool WarmStage>
void BasicSimulation<TraceType>::advance_to_impl(
    sim_time_t target_time, CustomFCFSScheduler *custom_scheduler) {
  if constexpr (!AccountResources) {
    (void)custom_scheduler;
  }

  // Validate precondition
  if (target_time < m_current_time) {
    throw std::runtime_error(
        "Cannot advance backwards in time. "
        "target_time=" +
        std::to_string(target_time) +
        " but current_time=" + std::to_string(m_current_time));
  }

  if constexpr (AccountResources) {
    custom_scheduler->advance_resource_accounting_to(m_current_time);
  }

  // Before entering the main loop, account for jobs submitted at the current
  // time and check whether any are eligible. This handles t=0 initially and
  // jobs appended at the current time by a streaming caller.
  m_scheduler->sync_to(m_current_time);
  record_queue_arrivals(m_current_time);
  const bool initial_capacity_changed = apply_capacity_changes(m_current_time);
  if constexpr (AccountResources) {
    if (initial_capacity_changed) {
      custom_scheduler->notify_resource_change();
    }
  }
  if (m_scheduler->has_eligible_jobs()) {
    // Call scheduler to evaluate newly arriving jobs
    while (true) {
      num_nodes_t free_nodes = get_available_nodes();
      auto jobs_to_run =
          m_scheduler->schedule(free_nodes, m_running_jobs, m_current_time);

      if (jobs_to_run.empty()) {
        break;
      }

      // Process ALL jobs returned by scheduler (backfilling can return
      // multiple)
      for (job_no_t job : jobs_to_run) {
        m_trace.insert_job(job, m_current_time);
        record_appended_job_start(job);
        const auto &record = m_trace.job_at(job);
        m_running_jobs[job] = {m_current_time,
                               static_cast<tdiff_t>(record.get_limit_time()),
                               record.get_num_nodes()};
        m_jobs_started++;

        // Records a resource-history sample internally (Trace's own
        // Context, via process_events_until()) - no separate call needed.
        m_trace.run_until_inclusive(m_current_time);
      }
    }
  }
  if constexpr (AccountResources) {
    custom_scheduler->commit_available_nodes(get_available_nodes());
  }

  // Main event loop - process events and make scheduling decisions until
  // complete Compute loop state variables once before entering loop
  size_t active_count = m_scheduler->active_job_count();
  sim_time_t next_arrival = m_scheduler->get_next_arrival_time();
  sim_time_t next_capacity =
      m_next_capacity_change < m_capacity_changes.size()
          ? m_capacity_changes[m_next_capacity_change].time
          : std::numeric_limits<sim_time_t>::max();

  m_queue_length_peak = std::max(m_queue_length_peak, active_count);

  // Continue while: (1) jobs are waiting, (2) jobs are running, or (3) jobs
  // will arrive. A finite streaming advance also consumes capacity changes
  // through its requested boundary even while idle. By contrast, batch mode
  // drains to max() and must not let unused schedule entries keep a completed
  // simulation alive or emit irrelevant resource rows.
  while (active_count > 0 || !m_trace.pending_events().empty() ||
         next_arrival < std::numeric_limits<sim_time_t>::max() ||
         (target_time < std::numeric_limits<sim_time_t>::max() &&
          next_capacity <= target_time)) {
    if (m_params.m_verbose) {
      std::cout << "Loop iter: active=" << active_count
                << " events=" << m_trace.pending_events().size()
                << " next_arrival=" << next_arrival
                << " time=" << m_current_time << std::endl;
    }
    // next_arrival already computed above

    // Find next replay event time
    bool has_replay_event = !m_trace.pending_events().empty();
    sim_time_t next_replay_time = std::numeric_limits<sim_time_t>::max();
    [[maybe_unused]] bool next_is_start = false;
    if (has_replay_event) {
      const auto &event = *m_trace.pending_events().begin();
      next_replay_time = convert_epoch<sim_time_t>(event.get_time());
      next_is_start = event.is_arrival();
    }

    // Decide which event to process
    bool should_schedule = false;

    if (has_replay_event && next_replay_time <= next_arrival &&
        next_replay_time <= next_capacity && next_replay_time <= target_time) {
      // Process replay events at this time
      // Advance time FIRST
      m_current_time = next_replay_time;
      advance_capacity_accounting_to(m_current_time);
      if constexpr (AccountResources) {
        custom_scheduler->advance_resource_accounting_to(m_current_time);
      }
      // Explicit sync: schedule() below only runs if an END event
      // freed resources (should_schedule = processed_end_event).
      // Without this call, a replay step that only processes START
      // events would leave the scheduler's eligibility tracking
      // stale relative to m_current_time, since nothing else
      // would sync it before the queries below.
      m_scheduler->sync_to(m_current_time);
      record_queue_arrivals(m_current_time);

      // Process ALL events at current_time before calling scheduler
      // This ensures END events are processed before START events created by
      // scheduler
      bool processed_end_event = false;

      while (!m_trace.pending_events().empty()) {
        const auto &event = *m_trace.pending_events().begin();
        sim_time_t event_time = convert_epoch<sim_time_t>(event.get_time());

        if (event_time != m_current_time) {
          break; // No more events at current_time
        }

        bool is_end = !event.is_arrival();
        job_no_t event_job_idx = event.get_job_idx();

        if (is_end) {
          if constexpr (WarmStage) {
            auto &job = m_trace.job_at(event_job_idx);
            const sim_time_t begin =
                convert_epoch<sim_time_t>(job.get_begin_time());
            if (begin < m_params.m_sim_start_time) {
              // Mutate before Trace processes the departure: a periodic flush
              // at this event can then neither write nor account the seed.
              job.suppress_output();
              if (m_pre_start_jobs == 0) {
                throw std::logic_error(
                    "warm-start historical-job count underflow");
              }
              --m_pre_start_jobs;
            } else {
              ++m_jobs_completed;
            }
          } else {
            ++m_jobs_completed;
          }
        }

        // Process this event (END or START) - records a
        // resource-history sample internally (Trace's own Context).
        m_trace.process_single_event();

        // If END event: remove from running_jobs
        if (is_end) {
          processed_end_event = true;
          m_running_jobs.erase(event_job_idx);
        }
      }

      const bool capacity_changed = apply_capacity_changes(m_current_time);

      if constexpr (AccountResources) {
        if (processed_end_event || capacity_changed) {
          custom_scheduler->notify_resource_change();
        }
      }

      // Only call scheduler if we processed END events (resources freed)
      should_schedule = processed_end_event || capacity_changed ||
                        next_arrival == m_current_time;

    } else if (next_arrival < std::numeric_limits<sim_time_t>::max() &&
               next_arrival <= next_capacity && next_arrival <= target_time) {
      // Job arrival - advance time FIRST
      // Note: Check next_arrival < infinity to avoid infinite loop
      // If no jobs arriving, scheduler should pick from waiting queue instead
      m_current_time = next_arrival;
      advance_capacity_accounting_to(m_current_time);
      if constexpr (AccountResources) {
        custom_scheduler->advance_resource_accounting_to(m_current_time);
      }
      // Explicit sync, matching the replay-event branch above for
      // symmetry - should_schedule is always true in this branch
      // (set unconditionally below), so schedule()'s own internal
      // sync_to() call would already cover this in practice, but
      // this doesn't rely on that.
      m_scheduler->sync_to(m_current_time);
      record_queue_arrivals(m_current_time);
      const bool capacity_changed = apply_capacity_changes(m_current_time);
      if constexpr (AccountResources) {
        if (capacity_changed) {
          custom_scheduler->notify_resource_change();
        }
      }

      // jobs_at_next_arrival already collected during wait_queue scan
      // TODO: Pass jobs_at_next_arrival to scheduler for efficient evaluation
      // For now, just set flag to schedule
      should_schedule = true;
    } else if (next_capacity < std::numeric_limits<sim_time_t>::max() &&
               next_capacity <= target_time) {
      m_current_time = next_capacity;
      advance_capacity_accounting_to(m_current_time);
      if constexpr (AccountResources) {
        custom_scheduler->advance_resource_accounting_to(m_current_time);
      }
      m_scheduler->sync_to(m_current_time);
      record_queue_arrivals(m_current_time);
      apply_capacity_changes(m_current_time);
      if constexpr (AccountResources) {
        custom_scheduler->notify_resource_change();
      }
      should_schedule = true;
    } else {
      // No arrivals and no replay events before target_time
      if (m_params.m_verbose) {
        std::cout << "ELSE block: active=" << m_scheduler->active_job_count()
                  << " events=" << m_trace.pending_events().size()
                  << " time=" << m_current_time << std::endl;
      }
      // No events to process - exit loop
      break;
    }

    // Scheduling loop - let scheduler make decisions after processing END
    // events
    if (should_schedule) {
      // Keep calling scheduler until it can't start any more jobs
      while (true) {
        num_nodes_t free_nodes = get_available_nodes();

        auto jobs_to_run =
            m_scheduler->schedule(free_nodes, m_running_jobs, m_current_time);

        if (jobs_to_run.empty()) {
          break; // Scheduler can't start anything else
        }

        // Process ALL jobs returned by scheduler (backfilling can return
        // multiple)
        for (job_no_t job : jobs_to_run) {
          m_trace.insert_job(job, m_current_time);
          record_appended_job_start(job);
          const auto &record = m_trace.job_at(job);
          m_running_jobs[job] = {m_current_time,
                                 static_cast<tdiff_t>(record.get_limit_time()),
                                 record.get_num_nodes()};
          m_jobs_started++;

          // Process this START event - records a resource-history
          // sample internally (Trace's own Context).
          while (!m_trace.pending_events().empty()) {
            const auto &event = *m_trace.pending_events().begin();
            sim_time_t event_time = convert_epoch<sim_time_t>(event.get_time());

            // Only process START events at current_time for this job
            if (event_time != m_current_time)
              break;
            if (!event.is_arrival())
              break; // Hit an END event, stop (shouldn't happen)

            m_trace.process_single_event();
            break; // Process only one START event per job
          }
        }
      }
    }
    if constexpr (AccountResources) {
      custom_scheduler->commit_available_nodes(get_available_nodes());
    }

    // Update loop state variables at end of iteration
    active_count = m_scheduler->active_job_count();
    next_arrival = m_scheduler->get_next_arrival_time();
    next_capacity = m_next_capacity_change < m_capacity_changes.size()
                        ? m_capacity_changes[m_next_capacity_change].time
                        : std::numeric_limits<sim_time_t>::max();

    // Peak queue length after all events and scheduling at this timestamp.
    m_queue_length_peak = std::max(m_queue_length_peak, active_count);

    // All peer events and scheduling decisions at this timestamp are settled.
    maybe_save_automatic_checkpoint(false);

    if constexpr (WarmStage) {
      // Transition only after every peer event and scheduling decision at the
      // last historical departure's timestamp has settled.
      if (m_pre_start_jobs == 0) {
        return;
      }
    }
  }

  // Loop exited - log final state for debugging
  if (m_params.m_verbose) {
    std::cout << "Loop exited: active=" << active_count
              << " events=" << m_trace.pending_events().size()
              << " time=" << m_current_time << std::endl;
  }

  // Don't record spurious final state - last event already recorded the final
  // state

  // Honor the documented postcondition (m_current_time == target_time)
  // even when the loop above exited early because nothing was left to
  // process before target_time (an idle gap - e.g. all currently-known
  // jobs finished, and the next arrival, if any, is later than
  // target_time). Without this, m_current_time stays stuck at the last
  // real event, silently understating elapsed time to any caller of
  // get_current_time() and weakening submit_job()'s own precondition
  // check (submit_time < m_current_time) against a stale value. This
  // is pure bookkeeping - every scheduling decision above was already
  // made using real event times, never target_time, so this can't
  // change any of them.
  const bool drain_to_completion =
      target_time == std::numeric_limits<sim_time_t>::max();
  if constexpr (AccountResources) {
    if (!drain_to_completion && m_trace.get_nodes_in_use() > 0) {
      custom_scheduler->advance_resource_accounting_to(target_time);
      advance_capacity_accounting_to(target_time);
    }
  }
  // max() is the internal/public drain sentinel, not a meaningful simulated
  // timestamp. After a drain, preserve the last real event time rather than
  // exposing max() (which also overflows integer-formatted CLI output).
  if (!drain_to_completion) {
    m_current_time = target_time;
  }
}

template <typename TraceType>
num_nodes_t BasicSimulation<TraceType>::get_nodes_in_use() const {
  return m_trace.get_nodes_in_use();
}

template <typename TraceType>
tdiff_t BasicSimulation<TraceType>::get_resource_area() const {
  if (m_custom_scheduler == nullptr) {
    throw std::logic_error(
        "resource-area accounting is available only with Custom FCFS");
  }
  return m_custom_scheduler->resource_area_through(m_current_time);
}

template <typename TraceType>
typename BasicSimulation<TraceType>::Backfill_Window
BasicSimulation<TraceType>::get_backfill_window() const {
  Backfill_Window window{
      m_current_time, get_available_nodes(), get_fcfs_head_shadow_time(), {}};

  // m_running_jobs stores the actual start time. The scheduler reserves
  // against each job's limit time, so this deliberately does the same.
  std::map<sim_time_t, num_nodes_t> releases_by_time;
  for (const auto &[job_idx, job] : m_running_jobs) {
    (void)job_idx;
    const sim_time_t end_time = job.start_time + job.run_time;
    if (end_time > m_current_time) {
      releases_by_time[end_time] += job.nodes;
    }
  }

  window.releases.reserve(releases_by_time.size());
  for (const auto &[time, nodes] : releases_by_time) {
    window.releases.push_back({time, nodes});
  }
  return window;
}

template <typename TraceType>
tdiff_t
BasicSimulation<TraceType>::get_prediction_horizon(double utilization) const {
  return m_scheduler->prediction_horizon(m_running_jobs, m_current_time,
                                         utilization);
}

template <typename TraceType>
typename BasicSimulation<TraceType>::Statistics
BasicSimulation<TraceType>::get_statistics() const {
  Statistics stats;

  // Basic counters
  stats.jobs_submitted = m_jobs_submitted;
  stats.jobs_started = m_jobs_started;
  stats.jobs_completed = m_jobs_completed;
  stats.jobs_running = m_running_jobs.size();
  stats.jobs_waiting = m_scheduler->active_job_count();
  stats.current_time = m_current_time;

  // Resource utilization
  stats.total_nodes = m_params.m_total_nodes;
  stats.nodes_in_use = get_nodes_in_use();
  stats.nodes_available = get_available_nodes();

  // Calculate wait times and turnaround times
  tdiff_t total_wait = 0.0;
  tdiff_t total_run_time = 0.0;
  tdiff_t total_turnaround = 0.0;
  double total_bounded_slowdown = 0.0;
  sim_time_t max_completion = 0.0;
  sim_time_t max_scheduled_completion = 0.0;
  num_jobs_t completed_count = 0;
  tdiff_t total_node_seconds = 0.0;

  for (const auto &job : m_trace.data()) {
    // Only count jobs that actually completed. Job_Record::is_scheduled()
    // (backed by a dedicated max-value sentinel) is the correct check
    // here: a job legitimately starting at simulation time 0 has
    // begin_time/end_time == 0 under the old convention, which the
    // previous begin_time-based check incorrectly treated as "never
    // started," silently excluding it from these averages. This
    // matches the same convention now used for m_jobs_completed
    // above (see the end-of-run() completion count).
    if (!job.is_scheduled()) {
      continue;
    }

    const sim_time_t completion = convert_epoch<sim_time_t>(job.get_end_time());
    max_scheduled_completion = std::max(max_scheduled_completion, completion);
    total_node_seconds +=
        static_cast<tdiff_t>(job.get_num_nodes()) * job.get_actual_run_time();
    if (completion <= stats.current_time) {
      tdiff_t wait = job.get_wait_time();
      tdiff_t exec = job.get_actual_run_time();

      total_wait += wait;
      total_run_time += exec;
      total_turnaround += (wait + exec);
      total_bounded_slowdown +=
          std::max(1.0, (wait + exec) /
                            std::max(exec, bounded_slowdown_threshold));
      max_completion = std::max(max_completion, completion);
      completed_count++;
    }
  }

  stats.avg_wait_time =
      (completed_count > 0) ? total_wait / completed_count : 0.0;
  stats.avg_run_time =
      (completed_count > 0) ? total_run_time / completed_count : 0.0;
  stats.avg_turnaround_time =
      (completed_count > 0) ? total_turnaround / completed_count : 0.0;
  stats.avg_bounded_slowdown =
      (completed_count > 0) ? total_bounded_slowdown / completed_count : 0.0;
  stats.makespan = max_completion;

  if (m_custom_scheduler != nullptr) {
    // Custom FCFS maintains live resource-area accounting at settled
    // scheduling boundaries. It remains accurate for partial streaming
    // snapshots and does not add bookkeeping to any other scheduler.
    stats.resource_area =
        m_custom_scheduler->resource_area_through(stats.current_time);
    const sim_time_t accounting_horizon =
        std::isfinite(stats.current_time) && stats.nodes_in_use > 0
            ? stats.current_time
            : m_custom_scheduler->m_resource_area_time;
    const tdiff_t capacity_area = capacity_area_through(accounting_horizon);
    stats.utilization =
        capacity_area > 0.0 ? stats.resource_area / capacity_area : 0.0;
  } else {
    // Preserve the original post-hoc statistic for standard schedulers.
    stats.resource_area = total_node_seconds + m_warm_resource_area;
    const sim_time_t accounting_end =
        std::max(max_scheduled_completion, m_warm_resource_end);
    const tdiff_t capacity_area = capacity_area_through(accounting_end);
    stats.utilization =
        capacity_area > 0.0 ? stats.resource_area / capacity_area : 0.0;
  }

  return stats;
}

template class BasicSimulation<Trace>;
template class BasicSimulation<PconTrace>;
template class BasicSimulation<SwfTrace>;

} // namespace dr_evt
