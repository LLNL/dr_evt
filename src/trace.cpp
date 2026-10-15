/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "common.hpp"
#include "params/trace_params.hpp"
#include "trace/job_stat_submit.hpp"
#include "trace/trace.hpp"
#include "utils/timer.hpp"

using std::cerr;
using std::cout;
using std::endl;

using namespace dr_evt;

/** @brief Load, replay, and report one trace according to a configuration.
 * @param[in] cfg Trace-processing configuration.
 * @return `EXIT_SUCCESS` on completion, or the nonzero trace-loading status.
 */
int process_trace(const dr_evt::Trace_Params &cfg) {
  Trace trace(cfg.get_infile());

  const auto max_num_jobs =
      cfg.is_max_jobs_set() ? cfg.max_num_jobs() : static_cast<num_jobs_t>(0u);

  // No .reserve() here anymore - load_data() sizes m_data's capacity
  // itself.
  int rc = trace.load_data(max_num_jobs);
  if (rc != EXIT_SUCCESS) {
    std::cerr << "trace loading not successful! : " << rc << std::endl;
    return rc;
  }
  std::cout << std::to_string(trace.data().size()) +
                   " jobs have been loaded.\n";
  trace.set_resource_history_capacity(cfg.get_resource_history_capacity());
  // Inclusive event-time boundary, exactly as the simulator applies its
  // own --max_time in Simulation::run(): events at or before the limit
  // are processed, later ones are not. --max_time used to be parsed but
  // never passed here.
  const sim_time_t run_limit = cfg.is_max_time_set()
                                   ? cfg.get_max_time()
                                   : std::numeric_limits<sim_time_t>::max();
  trace.run_job_trace(cfg.get_resource_trace(), cfg.get_total_nodes(),
                      run_limit);

  std::cout << "Trace ";
  trace.print_span(std::cout);

  if (!cfg.get_outfile().empty()) {
    std::ofstream of_trace(cfg.get_outfile());
    if (!of_trace) {
      throw std::runtime_error("Failed to open per-job output file: " +
                               cfg.get_outfile());
    }
    trace.print(of_trace);
  }

#if MARK_DAT_PERIOD
  if (!cfg.get_datfile().empty()) {
    std::ofstream of_dat(cfg.get_datfile());
    if (!of_dat) {
      throw std::runtime_error("Failed to open DAT-session output file: " +
                               cfg.get_datfile());
    }
    trace.print_DAT(of_dat);
  }
#endif

  if (!cfg.get_subfile().empty() || !cfg.get_subsumfile().empty()) {
    Job_Stat_Submit stat_sub;
    stat_sub.process(trace);
    std::cout << "Number of weeks: " +
                     std::to_string(stat_sub.get_num_weeks()) + "\n";

    if (!cfg.get_subfile().empty()) {
      std::ofstream of_stat_sub(cfg.get_subfile());
      if (!of_stat_sub) {
        throw std::runtime_error("Failed to open submission-statistics file: " +
                                 cfg.get_subfile());
      }
      stat_sub.print(of_stat_sub);
    }

    if (!cfg.get_subsumfile().empty()) {
      std::ofstream of_sub_summary(cfg.get_subsumfile());
      if (!of_sub_summary) {
        throw std::runtime_error("Failed to open submission-summary file: " +
                                 cfg.get_subsumfile());
      }
      stat_sub.print_summary(of_sub_summary);
    }
  }

  return rc;
}

/** @brief Run the command-line trace processor.
 * @param[in] argc Number of command-line arguments.
 * @param[in] argv Command-line argument vector.
 * @return `EXIT_SUCCESS` on completion, or `EXIT_FAILURE` when argument
 * parsing or trace processing fails. */
int main(int argc, char **argv) {
  int rc = EXIT_SUCCESS;
  dr_evt::Trace_Params cfg;
  // TODO: Use read_proto_params() instead
  if (!cfg.getopt(argc, argv)) {
    return EXIT_FAILURE;
  }

  double t_start = dr_evt::get_time();

  try {
    rc = process_trace(cfg);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    rc = EXIT_FAILURE;
  }

  std::cout << "Wall clock time to process trace: "
            << dr_evt::get_time() - t_start << " (sec)" << std::endl;

  return rc;
}
