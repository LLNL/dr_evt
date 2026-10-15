/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file trace_params.hpp
 * @brief Configuration for trace inspection, reporting, and resource output.
 */

#ifndef DR_EVT_PARAMS_TRACE_PARAMS_HPP
#define DR_EVT_PARAMS_TRACE_PARAMS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#include "dr_evt_types.hpp"
#include <string>

namespace dr_evt {
/** \addtogroup dr_evt_params
 *  @{ */

/** @brief Configuration for trace inspection and reporting tools. */
class Trace_Params {
public:
  /** @brief Construct trace-tool parameters with project defaults. */
  Trace_Params();
  /** @brief Parse trace-tool command-line options.
   * @param[in,out] argc Argument count.
   * @param[in,out] argv Argument vector.
   * @return true when parsing succeeds. */
  bool getopt(int &argc, char **&argv);
  /** @brief Print trace-tool usage and terminate.
   * @param[in] exec Executable name.
   * @param[in] code Process exit status. */
  void print_usage(const std::string exec, int code);
  /** @brief Write the effective configuration to standard output. */
  void print() const;
  /** @brief Return the input trace filename. @return Input path. */
  std::string get_infile() const { return m_infile; }
  /** @brief Set the optional per-job analysis output filename.
   * @param[in] ofname Output path, or an empty string to disable the report.
   */
  void set_outfile(const std::string &ofname);
  /** @brief Return the optional per-job analysis output filename.
   * @return Output path, or an empty string when the report is disabled. */
  std::string get_outfile() const { return m_outfile; }
  /** @brief Return the optional DAT-session output filename.
   * @return Output path, or an empty string when the report is disabled. */
  std::string get_datfile() const { return m_datfile; }
  /** @brief Return the optional detailed submission-statistics filename.
   * @return Output path, or an empty string when the report is disabled. */
  std::string get_subfile() const { return m_subfile; }
  /** @brief Return the optional submission-summary filename.
   * @return Output path, or an empty string when the report is disabled. */
  std::string get_subsumfile() const { return m_subsumfile; }
  /** @brief Return resource-history output filename. @return Output path. */
  std::string get_resource_trace() const { return m_resource_trace; }
  /** @brief Return resource pool size. @return Node count. */
  num_nodes_t get_total_nodes() const { return m_total_nodes; }
  /** @brief Return resource-history capacity. @return Entry capacity. */
  size_t get_resource_history_capacity() const {
    return m_resource_history_capacity;
  }

  /** @brief Return configured job limit. @return Maximum job count. */
  num_jobs_t max_num_jobs() const { return m_max_jobs; }
  /** @brief Report whether a job limit was supplied. @return true when set. */
  bool is_max_jobs_set() const { return m_is_jobs_set; }
  /** @brief Return the inclusive event-time boundary for replay.
   *  @return Maximum timestamp; meaningful only if is_max_time_set(). */
  sim_time_t get_max_time() const { return m_max_time; }
  /** @brief Report whether a time limit was supplied. @return true when set. */
  bool is_max_time_set() const { return m_is_time_set; }

  /// Maximum jobs to process.
  num_jobs_t m_max_jobs;
  /// Inclusive event-time boundary, in the same units and with the same
  /// meaning as the simulator's --max_time: events at or before this time
  /// are processed, later ones are not.
  sim_time_t m_max_time;

  /// Input trace filename.
  std::string m_infile;
  /// Optional per-job analysis output filename; empty disables the report.
  std::string m_outfile;
  std::string m_datfile;    ///< Optional DAT-session output filename.
  std::string m_subfile;    ///< Optional detailed submission-statistics file.
  std::string m_subsumfile; ///< Optional submission-summary file.
  std::string
      m_resource_trace; ///< Optional outfile name for resource-occupancy trace

  /// Initial capacity for the resource-history circular buffer (0 =
  /// size of the job trace). No overflow policy: every entry here is
  /// always immediately safe to reclaim, so reclaiming when full always
  /// succeeds - see Trace::m_resource_history's own comment.
  size_t m_resource_history_capacity;

  /// Pool size used only to derive free_nodes for the resource trace -
  /// the tracer itself does no scheduling and doesn't otherwise need this.
  num_nodes_t m_total_nodes;

  /// Whether m_max_jobs was explicitly configured.
  bool m_is_jobs_set;
  /// Whether m_max_time was explicitly configured.
  bool m_is_time_set;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_PARAMS_TRACE_PARAMS_HPP
