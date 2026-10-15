/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file trace_params.cpp
 * @brief Trace-tool command-line configuration implementation.
 */

#include "params/trace_params.hpp"
#include "trace/parse_utils.hpp"
#include "utils/file.hpp"
#include <cmath>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <string>

namespace dr_evt {

/** @brief getopt short-option specification for the tracer CLI. */
#define OPTIONS "d:hi:j:o:s:m:t:n:r:H:"
/** @brief Long-option table for the tracer CLI. */
static const struct option trace_longopts[] = {
    {"datfile", required_argument, 0, 'd'},
    {"help", no_argument, 0, 'h'},
    {"infile", required_argument, 0, 'i'},
    {"max_jobs", required_argument, 0, 'j'},
    {"outfile", required_argument, 0, 'o'},
    {"subfile", required_argument, 0, 's'},
    {"subsumf", required_argument, 0, 'm'},
    {"max_time", required_argument, 0, 't'},
    {"total_nodes", required_argument, 0, 'n'},
    {"resource_trace", required_argument, 0, 'r'},
    {"resource_history_capacity", required_argument, 0, 'H'},
    {0, 0, 0, 0},
};

Trace_Params::Trace_Params()
    : m_max_jobs(10u), m_max_time(dr_evt::max_sim_time), m_datfile(),
      m_subfile(),
      m_subsumfile(), m_resource_history_capacity(0),
      m_total_nodes(dr_evt::total_nodes), m_is_jobs_set(false),
      m_is_time_set(false) {}

bool Trace_Params::getopt(int &argc, char **&argv) {
  int c;
  m_is_jobs_set = false;
  m_is_time_set = false;

  if (argc < 2) {
    print_usage(argv[0], 0);
    return false;
  }

  // Parse an unsigned option value, rejecting a sign, trailing text, and
  // values above the destination type. atoi() returned 0 on garbage and
  // wrapped negatives; std::stoull() accepted a leading minus sign and
  // threw uncaught exceptions on garbage.
  auto parse_uint = [&](const char *name, unsigned long long max) {
    try {
      return parse_unsigned(optarg, max);
    } catch (const std::invalid_argument &) {
      std::cerr << "Error: " << name << " expects a non-negative integer no "
                << "greater than " << max << ", got '" << optarg << "'"
                << std::endl;
      print_usage(argv[0], 1);
      return 0ULL;
    }
  };
  while ((c = getopt_long(argc, argv, OPTIONS, trace_longopts, NULL)) != -1) {
    switch (c) {
    case 'd': /* --datfile */
      m_datfile = std::string(optarg);
      break;
    case 'h': /* --help */
      print_usage(argv[0], 0);
      break;
    case 'i': /* --infile */
      m_infile = std::string(optarg);
      break;
    case 'j': /* --max_jobs */
      // The type maximum is the internal "unlimited" sentinel; stop one below.
      m_max_jobs = static_cast<dr_evt::num_jobs_t>(parse_uint(
          "--max_jobs",
          std::numeric_limits<dr_evt::num_jobs_t>::max() - 1));
      m_is_jobs_set = true;
      break;
    case 'o': /* --outfile */
      m_outfile = std::string(optarg);
      break;
    case 's': /* --subfile */
      m_subfile = std::string(optarg);
      break;
    case 'm': /* --subsumf */
      m_subsumfile = std::string(optarg);
      break;
    case 't': /* --max_time */
      // Same parsing and validation as the simulator's --max_time.
      try {
        m_max_time = static_cast<dr_evt::sim_time_t>(std::stod(optarg));
      } catch (const std::exception &) {
        std::cerr << "Error: --max_time expects a number, got '" << optarg
                  << "'" << std::endl;
        print_usage(argv[0], 1);
        return false;
      }
      m_is_time_set = true;
      break;
    case 'n': /* --total_nodes */
      m_total_nodes = static_cast<dr_evt::num_nodes_t>(parse_uint(
          "--total_nodes", std::numeric_limits<dr_evt::num_nodes_t>::max()));
      break;
    case 'r': /* --resource_trace */
      m_resource_trace = std::string(optarg);
      break;
    case 'H': /* --resource_history_capacity */
      m_resource_history_capacity = parse_uint(
          "--resource_history_capacity", std::numeric_limits<size_t>::max());
      break;
    default:
      print_usage(argv[0], 1);
      return false;
      break;
    }
  }

  if (m_infile.empty() && (optind != (argc - 1))) {
    print_usage(argv[0], 1);
    return false;
  }

  if (optind == (argc - 1)) {
    if (!m_infile.empty()) {
      print_usage(argv[0], 1);
      return false;
    }
    m_infile = argv[optind];
  }
  if (m_is_time_set && (!std::isfinite(m_max_time) || m_max_time < 0.0)) {
    std::cerr << "Error: --max_time must be finite and nonnegative"
              << std::endl;
    print_usage(argv[0], 1);
    return false;
  }
  if (!m_is_jobs_set && m_is_time_set) {
    m_max_jobs = std::numeric_limits<decltype(m_max_jobs)>::max();
  }
  return true;
}

void Trace_Params::print_usage(const std::string exec, int code) {
  std::cerr
      << "Usage: " << exec
      << " inputfile\n"
         "    Run tracing on a job history file to extract statistics\n"
         "    upto a specified time or a number of jobs.\n"
         "\n"
         "  OPTIONS:\n"
         "    -d, --datfile\n"
         "        Optionally write detected DAT sessions.\n"
         "        No DAT report is written when this option is omitted.\n"
         "\n"
         "    -h, --help\n"
         "        Display this usage information\n"
         "\n"
         "    -i, --infile\n"
         "        Specify the input schedule file for replay.\n"
         "\n"
         "    -j, --max_jobs\n"
         "        Specify the maximum number of jobs to process.\n"
         "\n"
         "    -o, --outfile\n"
         "        Optionally write a CSV per-job analysis report.\n"
         "        No job report is written when this option is omitted.\n"
         "\n"
         "    -s, --subfile\n"
         "        Optionally write hourly submission statistics.\n"
         "        No detailed statistics are written when omitted.\n"
         "\n"
         "    -m, --subsumf\n"
         "        Optionally write submission statistics summarized by\n"
         "        hour of week. No summary is written when omitted.\n"
         "\n"
         "    -t, --max_time\n"
         "        Specify the upper timestamp limit to process.\n"
         "\n"
         "    -n, --total_nodes\n"
         "        Pool size for the resource trace. Only used to derive\n"
         "        free_nodes; the tracer does no scheduling.\n"
         "\n"
         "    -r, --resource_trace\n"
         "        Specify the output file name for the resource-occupancy\n"
         "        trace (time,free_nodes,allocated_nodes). Uses the\n"
         "        begin_time/end_time already in the trace directly - no\n"
         "        scheduler involved.\n"
         "\n"
         "    -H, --resource_history_capacity SIZE\n"
         "        Initial capacity of the resource-history circular buffer\n"
         "        (default: 0, meaning the size of the job trace).\n"
         "\n";
  exit(code);
}

void Trace_Params::print() const {
  using std::string;
  using std::to_string;
  string msg;
  msg = "------ Trace params ------\n";
  msg += " - max_jobs: " + to_string(m_max_jobs) + "\n";
  msg += " - max_time: " + to_string(m_max_time) + "\n";
  msg += " - infile: " + m_infile + "\n";
  msg += " - outfile: " + m_outfile + "\n";
  msg += " - datfile: " + m_datfile + "\n";
  msg += " - subfile: " + m_subfile + "\n";
  msg += " - subsumf: " + m_subsumfile + "\n";
  msg += " - total_nodes: " + to_string(m_total_nodes) + "\n";
  msg += " - resource_trace: " + m_resource_trace + "\n";
  msg += " - resource_history_capacity: " +
         std::to_string(m_resource_history_capacity) + "\n";
  msg += " - is_jobs_set: " + string{m_is_jobs_set ? "true" : "false"} + "\n";
  msg += " - is_time_set: " + string{m_is_time_set ? "true" : "false"} + "\n";

  std::cout << msg << std::endl;
}

void Trace_Params::set_outfile(const std::string &ofname) {
  m_outfile = ofname;
}

} // end of namespace dr_evt
