/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file sim_params.hpp
 * @brief Simulation configuration types, command-line parsing, and defaults.
 */

#ifndef DR_EVT_PARAMS_SIM_PARAMS_HPP
#define DR_EVT_PARAMS_SIM_PARAMS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#include "dr_evt_types.hpp"
#include "sim/scheduler_policies.hpp"
#include <string>
#include <vector>

namespace dr_evt {
/** \addtogroup dr_evt_params
 *  @{ */

/**
 * @brief Wait-queue implementation selection for FCFS scheduling.
 */
enum class QueueImplementation {
  CIRCULAR, ///< boost::circular_buffer implementation (default).
  DEQUE,    ///< std::deque implementation.
  MULTIMAP, ///< Differential-testing multimap implementation.
  BLOCK     ///< BlockWaitQueue implementation for large FCFS queues.
};

/**
 * What CircularBufferFCFSScheduler does when an insert would exceed its
 * wait queue's current capacity.
 */
enum class CircularOverflowPolicy {
  ABORT, ///< Throw std::runtime_error when capacity is exhausted.
  GROW   ///< Reallocate a larger queue and retain existing entries.
};

/**
 * @brief Job-trace data model selection.
 */
enum class TraceType {
  STANDARD, ///< Standard job/resource records (default).
  PCON,     ///< Pcon job/resource records with inline power values.
  SWF       ///< Standard Workload Format input (Parallel Workloads Archive).
};

/** @brief Complete configuration for a Simulation run or streaming session. */
class Sim_Params {
public:
  /** @brief Construct parameters with project defaults. */
  Sim_Params();
  /** @brief Parse and consume simulation command-line options.
   * @param[in,out] argc Argument count, updated for consumed options.
   * @param[in,out] argv Argument vector, updated for consumed options. */
  void getopt(int &argc, char **&argv);
  /** @brief Print command-line usage and terminate with a status code.
   * @param[in] exec Executable name.
   * @param[in] code Process exit status. */
  void print_usage(const std::string exec, int code);
  /** @brief Write the effective configuration to standard output. */
  void print() const;
  /** @brief Set the simulated-trace output filename. @param[in] ofname Output
   * path. */
  void set_outfile(const std::string &ofname);
  /**
   * @brief Configure progressive loading from a file containing trace paths.
   * @param[in] list_path File with one trace path per line.
   * @details Skips blank lines, trims trailing whitespace and CR characters,
   * populates m_infile_list_parsed, and assigns m_infile to its first entry
   * so Trace construction can validate a real header before run(). Shared by
   * the CLI and protobuf configuration paths.
   * @throws std::runtime_error when list_path cannot be read or names no files.
   */
  void set_infile_list(const std::string &list_path);
  /** @brief Return the simulated-trace output filename. @return Output path. */
  std::string get_outfile() const;
  /** @brief Set the resource-history output filename. @param[in] rfname Output
   * path. */
  void set_resource_trace(const std::string &rfname);
  /** @brief Return the resource-history output filename. @return Output path.
   */
  std::string get_resource_trace() const;

  /// Random seed used for stochastic runtime sampling.
  unsigned m_seed;
  /// Maximum jobs to load or simulate.
  dr_evt::num_jobs_t m_max_jobs;
  /// Maximum simulation time horizon.
  dr_evt::sim_time_t m_max_time;
  /// Global simulation start timestamp. Zero preserves the traditional run.
  dr_evt::sim_time_t m_sim_start_time;

  /// Primary input trace filename.
  std::string m_infile;
  std::string
      m_infile_list; ///< Path to a file listing multiple trace files, one per
                     ///< line, for progressive loading - empty means
                     ///< single-file mode via m_infile (unchanged)
  std::vector<std::string>
      m_infile_list_parsed; ///< Populated from m_infile_list during getopt()
                            ///< (one path per line, blank lines skipped) -
                            ///< m_infile is set to this list's first entry, so
                            ///< Trace's own constructor (which validates a
                            ///< file's header before Simulation::run() ever
                            ///< executes) always has a real file to check
                            ///< regardless of mode

  /// Whether m_max_jobs was explicitly configured.
  bool m_is_jobs_set;
  /// Whether m_max_time was explicitly configured.
  bool m_is_time_set;
  /// Whether -n/--total_nodes (or the config's total_nodes) was given;
  /// SWF input defaults to the header's MaxNodes otherwise.
  bool m_is_nodes_set;

  /// Backfill behavior applied by the scheduler.
  BackfillPolicy m_backfill_policy;
  /// Maximum feasible jobs offered to an experimental backfill selector.
  size_t m_num_max_candidates;
  /// Job priority/order policy applied by the scheduler.
  PriorityPolicy m_priority_policy;
  /// Wait-queue implementation used by FCFS scheduling.
  QueueImplementation m_queue_impl;
  /// Block size for the block queue; must be a power of two.
  size_t m_block_size;
  /// Initial circular wait-queue capacity; zero selects trace size.
  size_t m_wait_queue_capacity;
  /// Overflow action for the circular wait queue.
  CircularOverflowPolicy m_wait_queue_overflow;
  /// Initial trace job-store capacity; zero selects trace size.
  size_t m_job_store_capacity;
  /// Overflow action for the trace job store.
  CircularOverflowPolicy m_job_store_overflow;
  /// Completed-job output/reclamation interval; zero follows job-store
  /// capacity. Capacity pressure and explicit/final flushes override it.
  size_t m_job_flush_interval;
  /// Binary checkpoint destination; empty disables automatic checkpoints.
  std::string m_checkpoint_file;
  /// Completed-job interval for automatic checkpoints; zero disables it.
  num_jobs_t m_checkpoint_interval_jobs;
  /// Fraction of available system memory that triggers append rejection; zero
  /// disables it.
  double m_memory_pressure_fraction;
  /// Resource-history capacity; zero selects trace size. Entries are always
  /// reclaimable.
  size_t m_resource_history_capacity;
  /// Total nodes available to the simulated scheduler.
  num_nodes_t m_total_nodes;
  /// Optional CSV of time-varying capacity change points.
  std::string m_capacity_schedule;
  /// Job-trace data model: standard, Pcon, or SWF (see TraceType).
  TraceType m_trace_type;
  /// SWF only: processors per node used to convert allocated processors to
  /// nodes. 0 is not a divisor; it means "derive from the file header"
  /// (MaxProcs / MaxNodes, else 1), the same convention as the 0 defaults
  /// of the capacity options.
  unsigned m_cores_per_node;
  /// Input trace format name, such as "simple" or "lassen".
  std::string m_trace_format;
  /// Retained epoch/iso compatibility value; file input is auto-detected.
  std::string m_timestamp_format;
  /// IANA timezone used for timestamps without an embedded offset.
  std::string m_timezone;

  /// Source of observed runtime: trace value, sampled distribution, or limit.
  RunTimeMode m_run_time_mode;
  /// Distribution used when m_run_time_mode is DISTRIBUTION.
  DistributionType m_run_time_distribution;
  /// Runtime scale, for example 0.8 for 80 percent of estimated limit.
  double m_run_time_scale;
  /// Runtime distribution standard-deviation factor.
  double m_run_time_stddev;

  /// Enable verbose diagnostic output.
  bool m_verbose;
  /// Emit output timestamps with millisecond rather than integer-second
  /// precision.
  bool m_msec_output;

  /// Redis connection URI; empty selects ordinary file output.
  std::string m_redis_uri;
  /// Namespace used for the Redis CSV value, job hashes, and sorted indexes.
  std::string m_redis_key_prefix;

private:
  /// Simulated-trace output filename.
  std::string m_outfile;
  /// Optional resource-usage trace output filename.
  std::string m_resource_trace;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_PARAMS_SIM_PARAMS_HPP
