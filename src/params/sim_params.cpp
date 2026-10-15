/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file sim_params.cpp
 * @brief Simulation command-line configuration implementation.
 */

#include "params/sim_params.hpp"
#include "trace/parse_utils.hpp"
#include "utils/file.hpp"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <string>

#if defined(DR_EVT_HAS_PROTOBUF)
#include "proto/dr_evt_params.hpp"
#endif

namespace dr_evt {

static constexpr int OPT_TRACE_TYPE = 1000;
static constexpr int OPT_JOB_FLUSH_INTERVAL = 1001;
static constexpr int OPT_NUM_MAX_CANDIDATES = 1002;
static constexpr int OPT_CAPACITY_SCHEDULE = 1003;
static constexpr int OPT_SIM_START_TIME = 1004;
static constexpr int OPT_REDIS_URI = 1005;
static constexpr int OPT_REDIS_KEY_PREFIX = 1006;
static constexpr int OPT_CHECKPOINT_FILE = 1007;
static constexpr int OPT_CHECKPOINT_INTERVAL_JOBS = 1008;

/** @brief getopt short-option specification for the simulator CLI. */
#define OPTIONS "hi:j:n:o:s:t:b:p:q:Q:A:G:r:f:T:z:D:S:V:vc:R:MK:W:H:L:m:"
/** @brief Long-option table for the simulator CLI. */
static const struct option sim_longopts[] = {
    {"help", no_argument, 0, 'h'},
    {"infile", required_argument, 0, 'i'},
    {"infile_list", required_argument, 0, 'L'},
    {"max_jobs", required_argument, 0, 'j'},
    {"total_nodes", required_argument, 0, 'n'},
    {"capacity_schedule", required_argument, 0, OPT_CAPACITY_SCHEDULE},
    {"sim_start_time", required_argument, 0, OPT_SIM_START_TIME},
    {"outfile", required_argument, 0, 'o'},
    {"seed", required_argument, 0, 's'},
    {"max_time", required_argument, 0, 't'},
    {"backfill_policy", required_argument, 0, 'b'},
    {"num_max_candidates", required_argument, 0, OPT_NUM_MAX_CANDIDATES},
    {"priority_policy", required_argument, 0, 'p'},
    {"queue_impl", required_argument, 0, 'q'},
    {"block_size", required_argument, 0, 'Q'},
    {"wait_queue_capacity", required_argument, 0, 'A'},
    {"wait_queue_overflow", required_argument, 0, 'G'},
    {"job_store_capacity", required_argument, 0, 'K'},
    {"job_store_overflow", required_argument, 0, 'W'},
    {"job_flush_interval", required_argument, 0, OPT_JOB_FLUSH_INTERVAL},
    {"check_memory_pressure", required_argument, 0, 'm'},
    {"resource_history_capacity", required_argument, 0, 'H'},
    {"trace_type", required_argument, 0, OPT_TRACE_TYPE},
    {"trace_format", required_argument, 0, 'f'},
    {"timestamp_format", required_argument, 0, 'T'},
    {"timezone", required_argument, 0, 'z'},
    {"run_time_mode", required_argument, 0, 'r'},
    {"run_time_distribution", required_argument, 0, 'D'},
    {"run_time_scale", required_argument, 0, 'S'},
    {"run_time_stddev", required_argument, 0, 'V'},
    {"verbose", no_argument, 0, 'v'},
    {"msec_output", no_argument, 0, 'M'},
    {"config", required_argument, 0, 'c'},
    {"resource_trace", required_argument, 0, 'R'},
    {"redis_uri", required_argument, 0, OPT_REDIS_URI},
    {"redis_key_prefix", required_argument, 0, OPT_REDIS_KEY_PREFIX},
    {"checkpoint_file", required_argument, 0, OPT_CHECKPOINT_FILE},
    {"checkpoint_interval_jobs", required_argument, 0,
     OPT_CHECKPOINT_INTERVAL_JOBS},
    {0, 0, 0, 0},
};

Sim_Params::Sim_Params()
    : m_seed(0u), m_max_jobs(10u), m_max_time(dr_evt::max_sim_time),
      m_sim_start_time(0.0), m_is_jobs_set(false), m_is_time_set(false),
      m_backfill_policy(BackfillPolicy::EASY), m_num_max_candidates(1),
      m_priority_policy(PriorityPolicy::FCFS),
      m_queue_impl(QueueImplementation::CIRCULAR), m_block_size(128),
      m_wait_queue_capacity(0), // 0 = size of job trace (never overflows)
      m_wait_queue_overflow(CircularOverflowPolicy::GROW),
      m_job_store_capacity(0), // 0 = size of job trace (never overflows)
      m_job_store_overflow(CircularOverflowPolicy::GROW),
      m_job_flush_interval(0), m_checkpoint_interval_jobs(0),
      m_memory_pressure_fraction(0.0),
      m_resource_history_capacity(0), m_total_nodes(dr_evt::total_nodes),
      m_trace_type(TraceType::STANDARD),
      m_trace_format("simple"),    // Default to simple format
      m_timestamp_format("epoch"), // Retained timestamp compatibility value
      m_timezone("America/Los_Angeles"),    // Default timezone
      m_run_time_mode(RunTimeMode::ACTUAL), // Default: jobs run actual_run_time
                                            // from trace (most realistic)
      m_run_time_distribution(DistributionType::NORMAL),
      m_run_time_scale(1.0),  // Default: 100% of time_limit
      m_run_time_stddev(0.0), // Default: no variation
      m_verbose(false),       // Default: production mode (quiet)
      m_msec_output(false)    // Default: integer-second output
{}

void Sim_Params::getopt(int &argc, char **&argv) {
  int c;
  std::string sim_start_time_arg;
  auto apply_sim_start_time = [&]() {
    if (sim_start_time_arg.empty()) {
      return;
    }
    const char *old_tz_ptr = std::getenv("TZ");
    const bool had_old_tz = old_tz_ptr != nullptr;
    const std::string old_tz = had_old_tz ? old_tz_ptr : "";
    try {
      setenv("TZ", m_timezone.c_str(), 1);
      tzset();
      size_t parsed_chars = 0;
      bool parsed_numeric = false;
      try {
        const double numeric = std::stod(sim_start_time_arg, &parsed_chars);
        if (parsed_chars == sim_start_time_arg.size()) {
          m_sim_start_time = numeric;
          parsed_numeric = true;
        }
      } catch (const std::exception &) {
        // Fall through to ISO timestamp parsing.
      }
      if (!parsed_numeric) {
        epoch_t parsed;
        set_by(parsed, sim_start_time_arg);
        m_sim_start_time = convert_epoch<sim_time_t>(parsed);
      }
    } catch (const std::exception &e) {
      if (had_old_tz) {
        setenv("TZ", old_tz.c_str(), 1);
      } else {
        unsetenv("TZ");
      }
      tzset();
      std::cerr << "Error: invalid --sim_start_time: " << e.what() << std::endl;
      print_usage(argv[0], 1);
    }
    if (had_old_tz) {
      setenv("TZ", old_tz.c_str(), 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
    if (!std::isfinite(m_sim_start_time) || m_sim_start_time < 0.0) {
      std::cerr << "Error: --sim_start_time must be finite and nonnegative"
                << std::endl;
      print_usage(argv[0], 1);
    }
    sim_start_time_arg.clear();
  };
  m_is_jobs_set = false;
  m_is_time_set = false;

  // Set only by -i/--infile on the command line. A config file may also
  // fill m_infile, and a positional argument is allowed to override that.
  bool infile_option = false;
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
  while ((c = getopt_long(argc, argv, OPTIONS, sim_longopts, NULL)) != -1) {
    switch (c) {
    case 'h': /* --help */
      print_usage(argv[0], 0);
      break;
    case 'i': /* --infile */
      m_infile = std::string(optarg);
      infile_option = true;
      break;
    case 'L': /* --infile_list */
      m_infile_list = std::string(optarg);
      break;
    case 'j': /* --max_jobs */
      // The type maximum is the internal "unlimited" sentinel; stop one below.
      m_max_jobs = static_cast<dr_evt::num_jobs_t>(parse_uint(
          "--max_jobs",
          std::numeric_limits<dr_evt::num_jobs_t>::max() - 1));
      m_is_jobs_set = true;
      break;
    case 'n': /* --total_nodes */
      m_total_nodes = static_cast<num_nodes_t>(parse_uint(
          "--total_nodes", std::numeric_limits<num_nodes_t>::max()));
      break;
    case OPT_CAPACITY_SCHEDULE: /* --capacity_schedule */
      m_capacity_schedule = std::string(optarg);
      break;
    case OPT_SIM_START_TIME: /* --sim_start_time */
      // Parse after all options so --timezone is order-independent.
      sim_start_time_arg = optarg;
      break;
    case 'o': /* --outfile */
      m_outfile = std::string(optarg);
      break;
    case OPT_CHECKPOINT_FILE:
      m_checkpoint_file = optarg;
      break;
    case OPT_CHECKPOINT_INTERVAL_JOBS:
      m_checkpoint_interval_jobs = parse_uint(
          "--checkpoint_interval_jobs",
          std::numeric_limits<dr_evt::num_jobs_t>::max());
      if (m_checkpoint_interval_jobs == 0) {
        std::cerr
            << "Error: --checkpoint_interval_jobs must be greater than zero\n";
        print_usage(argv[0], 1);
      }
      break;
    case 's': /* --seed */
      m_seed = static_cast<unsigned>(
          parse_uint("--seed", std::numeric_limits<unsigned>::max()));
      break;
    case 't': /* --max_time */
      try {
        m_max_time = static_cast<dr_evt::sim_time_t>(std::stod(optarg));
      } catch (const std::exception &) {
        std::cerr << "Error: --max_time expects a number, got '" << optarg
                  << "'" << std::endl;
        print_usage(argv[0], 1);
      }
      m_is_time_set = true;
      break;
    case 'b': /* --backfill_policy */
    {
      std::string policy(optarg);
      if (policy.empty()) {
        m_backfill_policy = BackfillPolicy::EASY;
      } else if (policy == "easy") {
        m_backfill_policy = BackfillPolicy::EASY;
      } else if (policy == "conservative") {
        m_backfill_policy = BackfillPolicy::CONSERVATIVE;
      } else if (policy == "none") {
        m_backfill_policy = BackfillPolicy::NONE;
      } else {
        std::cerr << "Unknown backfill policy: " << policy << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case OPT_NUM_MAX_CANDIDATES: /* --num_max_candidates */
      m_num_max_candidates = parse_uint(
          "--num_max_candidates", std::numeric_limits<size_t>::max());
      if (m_num_max_candidates == 0) {
        std::cerr << "Error: --num_max_candidates must be greater than zero"
                  << std::endl;
        print_usage(argv[0], 1);
      }
      break;
    case 'p': /* --priority_policy */
    {
      std::string policy(optarg);
      if (policy.empty()) {
        m_priority_policy = PriorityPolicy::FCFS;
      } else if (policy == "fcfs") {
        m_priority_policy = PriorityPolicy::FCFS;
      } else if (policy == "fcfs_alt") {
        m_priority_policy = PriorityPolicy::FCFS_ALT;
      } else if (policy == "fcfs_conservative") {
        m_priority_policy = PriorityPolicy::FCFS_CONSERVATIVE;
      } else if (policy == "sjf") {
        m_priority_policy = PriorityPolicy::SJF;
      } else if (policy == "ljf") {
        m_priority_policy = PriorityPolicy::LJF;
      } else {
        std::cerr << "Unknown priority policy: " << policy << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case 'q': /* --queue_impl */
    {
      std::string impl(optarg);
      if (impl.empty()) {
        m_queue_impl = QueueImplementation::CIRCULAR;
      } else if (impl == "circular") {
        m_queue_impl = QueueImplementation::CIRCULAR;
      } else if (impl == "deque") {
        m_queue_impl = QueueImplementation::DEQUE;
      } else if (impl == "multimap") {
        m_queue_impl = QueueImplementation::MULTIMAP;
      } else if (impl == "block") {
        m_queue_impl = QueueImplementation::BLOCK;
      } else {
        std::cerr << "Unknown queue implementation: " << impl << std::endl;
        std::cerr << "Valid options: 'circular' (default), 'deque', "
                     "'multimap', 'block' (FCFS only)"
                  << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case 'Q': /* --block_size */
    {
      m_block_size =
          parse_uint("--block_size", std::numeric_limits<size_t>::max());
      // Validate power of 2
      if (m_block_size == 0 || (m_block_size & (m_block_size - 1)) != 0) {
        std::cerr << "Error: block_size must be a power of 2 (e.g., 4, 8, 16, "
                     "32, 64, 128, 256)"
                  << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case 'A': /* --wait_queue_capacity */
    {
      m_wait_queue_capacity = parse_uint(
          "--wait_queue_capacity", std::numeric_limits<size_t>::max());
    } break;
    case 'G': /* --wait_queue_overflow */
    {
      std::string policy(optarg);
      if (policy.empty()) {
        m_wait_queue_overflow = CircularOverflowPolicy::GROW;
      } else if (policy == "abort") {
        m_wait_queue_overflow = CircularOverflowPolicy::ABORT;
      } else if (policy == "grow") {
        m_wait_queue_overflow = CircularOverflowPolicy::GROW;
      } else {
        std::cerr << "Unknown wait_queue_overflow policy: " << policy
                  << std::endl;
        std::cerr << "Valid options: 'abort', 'grow' (default)" << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case 'K': /* --job_store_capacity */
    {
      m_job_store_capacity = parse_uint(
          "--job_store_capacity", std::numeric_limits<size_t>::max());
    } break;
    case 'W': /* --job_store_overflow */
    {
      std::string policy(optarg);
      if (policy.empty()) {
        m_job_store_overflow = CircularOverflowPolicy::GROW;
      } else if (policy == "abort") {
        m_job_store_overflow = CircularOverflowPolicy::ABORT;
      } else if (policy == "grow") {
        m_job_store_overflow = CircularOverflowPolicy::GROW;
      } else {
        std::cerr << "Unknown job_store_overflow policy: " << policy
                  << std::endl;
        std::cerr << "Valid options: 'abort', 'grow' (default)" << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case OPT_JOB_FLUSH_INTERVAL: /* --job_flush_interval */
      m_job_flush_interval = parse_uint(
          "--job_flush_interval", std::numeric_limits<size_t>::max());
      break;
    case 'm': /* --check_memory_pressure */
    {
      double fraction = 0.0;
      try {
        fraction = std::stod(optarg);
      } catch (const std::exception &) {
        std::cerr << "Invalid --check_memory_pressure fraction: '" << optarg
                  << "' (not a number)" << std::endl;
        print_usage(argv[0], 1);
      }
      if (!(fraction > 0.0) || fraction > 1.0) {
        std::cerr << "Invalid --check_memory_pressure fraction: " << fraction
                  << " (must be > 0.0 and <= 1.0)" << std::endl;
        print_usage(argv[0], 1);
      }
      m_memory_pressure_fraction = fraction;
    } break;
    case 'H': /* --resource_history_capacity */
    {
      m_resource_history_capacity = parse_uint(
          "--resource_history_capacity", std::numeric_limits<size_t>::max());
    } break;
    case OPT_TRACE_TYPE: /* --trace_type */
    {
      std::string type(optarg);
      if (type.empty() || type == "standard") {
        m_trace_type = TraceType::STANDARD;
      } else if (type == "pcon") {
        m_trace_type = TraceType::PCON;
      } else {
        std::cerr << "Unknown trace type: " << type << std::endl;
        std::cerr << "Valid options: 'standard' (default), 'pcon'" << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case 'f': /* --trace_format */
    {
      std::string format(optarg);
      if (format.empty()) {
        m_trace_format = "lassen";
      } else if (format == "simple" || format == "lassen") {
        m_trace_format = format;
      } else {
        std::cerr << "Unknown trace format: " << format << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case 'T': /* --timestamp_format */
    {
      std::string format(optarg);
      if (format.empty()) {
        m_timestamp_format = "epoch";
      } else if (format == "epoch" || format == "iso") {
        m_timestamp_format = format;
      } else {
        std::cerr << "Unknown timestamp format: " << format << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case 'z': /* --timezone */
      m_timezone = optarg;
      break;
    case 'r': /* --run_time_mode */
    {
      std::string mode(optarg);
      if (mode.empty()) {
        m_run_time_mode = RunTimeMode::ACTUAL; // default
      } else if (mode == "actual") {
        m_run_time_mode = RunTimeMode::ACTUAL;
      } else if (mode == "distribution") {
        m_run_time_mode = RunTimeMode::DISTRIBUTION;
      } else if (mode == "limit") {
        m_run_time_mode = RunTimeMode::LIMIT;
      } else {
        std::cerr << "ERROR: Invalid --run_time_mode: " << mode << "\n";
        std::cerr << "       Valid values: actual, distribution, limit\n";
        print_usage(argv[0], 1);
      }
    } break;
    case 'D': /* --run_time_distribution */
    {
      std::string dist(optarg);
      if (dist.empty()) {
        m_run_time_distribution = DistributionType::NORMAL;
      } else if (dist == "normal") {
        m_run_time_distribution = DistributionType::NORMAL;
      } else if (dist == "lognormal") {
        m_run_time_distribution = DistributionType::LOGNORMAL;
      } else if (dist == "uniform") {
        m_run_time_distribution = DistributionType::UNIFORM;
      } else {
        std::cerr << "Unknown distribution type: " << dist << std::endl;
        print_usage(argv[0], 1);
      }
    } break;
    case 'S': /* --run_time_scale */
      m_run_time_scale = std::stod(optarg);
      break;
    case 'V': /* --run_time_stddev */
      m_run_time_stddev = std::stod(optarg);
      break;
    case 'v': /* --verbose */
      m_verbose = true;
      break;
    case 'M': /* --msec_output */
      m_msec_output = true;
      break;
    case 'c': /* --config */
    {
#if defined(DR_EVT_HAS_PROTOBUF)
      // Preserve left-to-right precedence: a following config replaces any
      // earlier command-line simulation start time.
      apply_sim_start_time();
      std::string config_file(optarg);
      try {
        read_proto_params(config_file, *this, m_verbose);
      } catch (const std::exception &e) {
        std::cerr << "Error reading --config file: " << e.what() << std::endl;
        exit(1);
      }
#else
      std::cerr << "Error: --config option requires Protobuf support"
                << std::endl;
      std::cerr << "Rebuild with -DDR_EVT_ENABLE_PROTOBUF=ON" << std::endl;
      exit(1);
#endif
    } break;
    case 'R': /* --resource_trace */
      m_resource_trace = std::string(optarg);
      break;
    case OPT_REDIS_URI: /* --redis_uri */
      m_redis_uri = optarg;
      break;
    case OPT_REDIS_KEY_PREFIX: /* --redis_key_prefix */
      m_redis_key_prefix = optarg;
      break;
    default:
      print_usage(argv[0], 1);
      break;
    }
  }

  apply_sim_start_time();

  // --infile_list mode has no single positional trace file to require -
  // the file it names lists several instead. Otherwise the trace comes
  // from either -i/--infile or exactly one positional argument, the same
  // rule the tracer applies: one of them is required, and giving both is
  // an error rather than silently preferring one.
  if (m_infile_list.empty()) {
    const bool has_positional = (optind == (argc - 1));
    if (has_positional) {
      if (infile_option) {
        std::cerr << "Error: give the input trace either as -i/--infile or "
                     "as the positional argument, not both"
                  << std::endl;
        print_usage(argv[0], 1);
      }
      m_infile = argv[optind];
    } else if (optind != argc || m_infile.empty()) {
      print_usage(argv[0], 1);
    }
  } else if (optind != argc) {
    print_usage(argv[0], 1);
  } else {
    try {
      set_infile_list(m_infile_list);
    } catch (const std::exception &e) {
      std::cerr << e.what() << std::endl;
      print_usage(argv[0], 1);
    }
  }
  // Deriving the default output name throws when the input itself ends in
  // ".out", since the run would overwrite its own input. Report that as an
  // ordinary usage error rather than letting it escape main() and abort.
  try {
    set_outfile(m_outfile);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl
              << "Input files ending in '.out' need an explicit output name; "
                 "pass -o/--outfile."
              << std::endl;
    print_usage(argv[0], 1);
  }

  if (m_redis_uri.empty() != m_redis_key_prefix.empty()) {
    std::cerr << "Error: --redis_uri and --redis_key_prefix must be specified "
                 "together"
              << std::endl;
    print_usage(argv[0], 1);
  }
  if (m_checkpoint_file.empty() && m_checkpoint_interval_jobs != 0) {
    std::cerr << "Error: --checkpoint_interval_jobs requires --checkpoint_file"
              << std::endl;
    print_usage(argv[0], 1);
  }
#if !defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
  if (!m_redis_uri.empty()) {
    std::cerr << "Error: Redis output requires a build configured with "
                 "-DDR_EVT_WITH_REDIS=ON"
              << std::endl;
    print_usage(argv[0], 1);
  }
#endif

  if (m_is_time_set && (!std::isfinite(m_max_time) || m_max_time < 0.0)) {
    std::cerr << "Error: --max_time must be finite and nonnegative"
              << std::endl;
    print_usage(argv[0], 1);
  }

  if (!m_is_jobs_set && m_is_time_set) {
    m_max_jobs = std::numeric_limits<decltype(m_max_jobs)>::max();
  }
}

void Sim_Params::print_usage(const std::string exec, int code) {
  std::cerr
      << "Usage: " << exec
      << " inputfile\n"
         "    Run a job scheduling simulation with backfilling.\n"
         "    Simulates job scheduling and resource allocation\n"
         "    for the given trace file.\n"
         "\n"
         "  OPTIONS:\n"
         "    -h, --help\n"
         "        Display this usage information\n"
         "\n"
         "    -i, --infile\n"
         "        Specify the input file name for simulation.\n"
         "\n"
         "    -L, --infile_list FILENAME\n"
         "        Path to a file listing multiple trace files, one per line -\n"
         "        progressive loading: each is loaded in turn as the "
         "simulation\n"
         "        reaches it, so --job_store_capacity can actually bound "
         "memory\n"
         "        (unlike single-file mode, which always grows to fit the "
         "whole\n"
         "        trace). Files must already be sorted by submit_time, both\n"
         "        within each file and across the sequence (each file's "
         "earliest\n"
         "        submit_time >= the previous file's latest). Mutually "
         "exclusive\n"
         "        with the positional trace-file argument and -i/--infile - "
         "do\n"
         "        not provide both. See\n"
         "        docs/dev/OUTPUT_TRACE_BUFFERS.md.\n"
         "\n"
         "    -j, --max_jobs\n"
         "        Specify the maximum number of jobs to run.\n"
         "\n"
         "    -n, --total_nodes\n"
         "        Specify total number of nodes in the system (default: 795).\n"
         "\n"
         "    --capacity_schedule FILENAME\n"
         "        CSV change points with columns time,total_nodes. Capacity\n"
         "        defaults to --total_nodes before the first row. Reductions\n"
         "        are non-preemptive: running jobs finish, and new starts\n"
         "        wait until they fit. Jobs are rejected only when they\n"
         "        exceed --total_nodes, not the scheduled capacity. A zero\n"
         "        value pauses new starts.\n"
         "\n"
         "    --sim_start_time TIME\n"
         "        Set the global simulation start time as nonnegative epoch\n"
         "        seconds or an ISO timestamp. With replay-format\n"
         "        input, jobs that began before TIME seed live resource state\n"
         "        without entering output or job statistics. Their remaining\n"
         "        departures still release resources normally.\n"
         "\n"
         "    -o, --outfile\n"
         "        Specify the output file name for simulation.\n"
         "\n"
         "    --checkpoint_file FILENAME\n"
         "        Write automatic restart checkpoints to FILENAME.\n"
         "        Progressive loading checkpoints at every completed input\n"
         "        file boundary; --checkpoint_interval_jobs adds periodic\n"
         "        checkpoints after settled event timestamps.\n"
         "\n"
         "    --checkpoint_interval_jobs COUNT\n"
         "        Checkpoint after each COUNT additional completed jobs.\n"
         "        Requires --checkpoint_file.\n"
         "\n"
         "    --redis_uri URI\n"
         "        Write simulated-job and resource-history output to Redis\n"
         "        instead of --outfile and --resource_trace. Requires\n"
         "        --redis_key_prefix and a build\n"
         "        configured with -DDR_EVT_WITH_REDIS=ON.\n"
         "\n"
         "    --redis_key_prefix PREFIX\n"
         "        Redis namespace for job/resource CSV values, per-job\n"
         "        hashes, and sorted indexes. Requires --redis_uri.\n"
         "\n"
         "    -s, --seed\n"
         "        Specify the seed for random number generator. Without this,\n"
         "        the deterministic default seed is 0.\n"
         "\n"
         "    -t, --max_time\n"
         "        Stop batch simulation after processing all events at this\n"
         "        nonnegative simulation timestamp.\n"
         "\n"
         "    -b, --backfill_policy {easy|conservative|none}\n"
         "        Backfilling policy (default: easy).\n"
         "        easy: Only first job gets reservation\n"
         "        conservative: All jobs get reservations\n"
         "        none: Backfilling disabled - jobs run strictly in FCFS "
         "order\n"
         "\n"
         "    --num_max_candidates COUNT\n"
         "        Maximum feasible jobs offered to the experimental external\n"
         "        backfill selector per decision (default: 1). The callback\n"
         "        itself is installed through the C++ or Python API.\n"
         "\n"
         "    -p, --priority_policy {fcfs|fcfs_alt|fcfs_conservative|sjf|ljf}\n"
         "        Job priority/ordering policy (default: fcfs).\n"
         "        fcfs: First-Come-First-Served\n"
         "        fcfs_alt: Alternative FCFS implementation (for testing)\n"
         "        fcfs_conservative: FCFS with conservative/no backfilling "
         "support\n"
         "        sjf: Shortest-Job-First\n"
         "        ljf: Longest-Job-First\n"
         "\n"
         "    -q, --queue_impl {circular|deque|multimap|block}\n"
         "        Wait queue implementation for FCFS scheduler (default: "
         "circular).\n"
         "        circular: boost::circular_buffer-based (fastest measured; "
         "default)\n"
         "        deque: std::deque-based (simple, well-tested)\n"
         "        multimap: std::multimap-based (for differential testing)\n"
         "        block: Block-based with multi-index (optimized for large "
         "queues)\n"
         "        Note: Only applies to FCFS scheduler. SJF/LJF always use "
         "multimap.\n"
         "\n"
         "    -Q, --block_size SIZE\n"
         "        Block size for block queue implementation (default: 128).\n"
         "        Must be power of 2: 4, 8, 16, 32, 64, 128, or 256\n"
         "        Only used when --queue_impl=block\n"
         "        Larger blocks reduce overhead but increase memory per block\n"
         "\n"
         "    -A, --wait_queue_capacity SIZE\n"
         "        Initial capacity of the wait queue (default: 0, meaning the\n"
         "        size of the job trace - large enough it can never "
         "overflow).\n"
         "        Only used when --queue_impl=circular\n"
         "\n"
         "    -G, --wait_queue_overflow {abort|grow}\n"
         "        What to do if an insert would exceed wait_queue_capacity\n"
         "        (default: grow). abort: end the simulation with an error.\n"
         "        grow: reallocate to a larger capacity, copying existing\n"
         "        entries over.\n"
         "        Only used when --queue_impl=circular\n"
         "\n"
         "    -K, --job_store_capacity SIZE\n"
         "        Initial capacity of the job-record store (Trace::m_data, a\n"
         "        boost::circular_buffer). In single-file mode (the default),\n"
         "        capacity is set to fit the whole trace during loading\n"
         "        regardless of this setting - use --infile_list for a\n"
         "        capacity that can actually bound memory; see\n"
         "        docs/dev/OUTPUT_TRACE_BUFFERS.md.\n"
         "        Default: 0, meaning the size of the job trace - large\n"
         "        enough it never needs to grow.\n"
         "\n"
         "    -W, --job_store_overflow {abort|grow}\n"
         "        What to do if an insert would exceed job_store_capacity\n"
         "        (default: grow). abort: end the simulation with an error.\n"
         "        grow: reallocate to a larger capacity, copying existing\n"
         "        entries over.\n"
         "\n"
         "    --job_flush_interval RECORDS\n"
         "        Opportunistically write and reclaim the completed front\n"
         "        prefix after this many additional job departures. The\n"
         "        default is 0, meaning the current job-store capacity.\n"
         "        Capacity pressure, an explicit flush, and final output\n"
         "        remain independent flush triggers and reset this interval.\n"
         "\n"
         "    -m, --check_memory_pressure FRACTION\n"
         "        Before growing the job-record store for a new batch\n"
         "        (--infile_list progressive loading, or a batch appended\n"
         "        via the streaming API), refuse if doing so would push\n"
         "        projected peak usage past FRACTION of actual available\n"
         "        system memory (Linux only, via /proc/meminfo; a no-op\n"
         "        elsewhere, since there's nothing to check against).\n"
         "        FRACTION must be > 0.0 and <= 1.0 (e.g. 0.8 for 80%).\n"
         "        Independent of --job_store_overflow: applies regardless\n"
         "        of whether that is set to abort or grow. Disabled by\n"
         "        default (no argument means disabled - this option must\n"
         "        be given a value to take effect at all).\n"
         "        See docs/dev/OUTPUT_TRACE_BUFFERS.md.\n"
         "\n"
         "    -H, --resource_history_capacity SIZE\n"
         "        Initial capacity of the resource-history circular buffer\n"
         "        (default: 0, meaning 2x the number of loaded jobs, floored\n"
         "        at 4096 - large enough it never needs reclaiming purely to\n"
         "        make room). No overflow policy: every entry here is always\n"
         "        immediately safe to reclaim, so reclaiming on a full buffer\n"
         "        always succeeds.\n"
         "\n"
         "    --trace_type {standard|pcon}\n"
         "        Job-trace data model (default: standard).\n"
         "        standard: standard job records and resource-history "
         "samples.\n"
         "        pcon: experimental records with avgpcon, minpcon, and\n"
         "        maxpcon stored inline in each Pcon job record.\n"
         "        This is independent of --trace_format, which controls the\n"
         "        input-file parser/layout.\n"
         "\n"
         "    -f, --trace_format {simple|lassen}\n"
         "        Trace file format (default: simple).\n"
         "        simple: CSV, columns looked up by name in the header row.\n"
         "          Simulation mode (no begin_time/end_time columns): "
         "requires\n"
         "          job_submit_time (or submit_time), num_nodes, and\n"
         "          time_limit (or timelimit or walltime).\n"
         "          Replay mode (begin_time and end_time present): requires\n"
         "          job_submit_time (or submit_time), begin_time, end_time, "
         "num_nodes, and\n"
         "          time_limit. The queue field is optional in both modes.\n"
         "        lassen: 33-column LLNL Lassen format\n"
         "\n"
         "    -T, --timestamp_format {epoch|iso}\n"
         "        Compatibility setting (default: epoch). Input timestamps\n"
         "        are auto-detected as epoch seconds or calendar timestamps;\n"
         "        simulator output is numeric in either setting.\n"
         "\n"
         "    -z, --timezone TIMEZONE\n"
         "        Timezone for timestamp parsing (default: "
         "America/Los_Angeles).\n"
         "        Examples: UTC, America/New_York, America/Los_Angeles\n"
         "        Used for calendar timestamps without an embedded offset.\n"
         "\n"
         "    -r, --run_time_mode {actual|distribution|limit}\n"
         "        How to determine the job's actual run time in simulation "
         "mode (default: actual).\n"
         "        actual: Read from actual_run_time column in trace (most "
         "realistic)\n"
         "          (also accepted aliases: actual_runtime, duration,\n"
         "          actual_duration, run_time)\n"
         "        limit: Jobs run exactly time_limit (unrealistic, for "
         "debugging/testing)\n"
         "        distribution: Sample from statistical distribution "
         "(realistic with variation)\n"
         "        NOTE: Scheduler uses time_limit as the best estimator for "
         "planning (realistic mode).\n"
         "\n"
         "    -D, --run_time_distribution {normal|lognormal|uniform}\n"
         "        Distribution type when run_time_mode=distribution (default: "
         "normal).\n"
         "        normal: Normal distribution N(limit*scale, limit*stddev)\n"
         "        lognormal: Lognormal with median=limit*scale\n"
         "        uniform: Uniform in [limit*scale, limit*(scale+stddev)]\n"
         "\n"
         "    -S, --run_time_scale FACTOR\n"
         "        Scale factor for run time sampling (default: 1.0).\n"
         "        Example: 0.8 means jobs run 80% of their time_limit on "
         "average\n"
         "\n"
         "    -V, --run_time_stddev FACTOR\n"
         "        Standard deviation factor for run time sampling (default: "
         "0.0).\n"
         "        For normal: std dev = limit * stddev\n"
         "        For lognormal: shape parameter\n"
         "        For uniform: upper bound offset\n"
         "\n"
         "    -v, --verbose\n"
         "        Enable verbose output for debugging and testing.\n"
         "        Shows detailed simulation progress, scheduling decisions,\n"
         "        and resource usage. Disabled by default for production "
         "runs.\n"
         "\n"
         "    -M, --msec_output\n"
         "        Output timestamps (in the simulated trace and resource "
         "trace\n"
         "        files) with millisecond precision (3 decimal places) "
         "instead\n"
         "        of truncating to whole seconds. Disabled by default - "
         "existing\n"
         "        traces and tests assume integer-second timestamps.\n"
         "\n"
#if defined(DR_EVT_HAS_PROTOBUF)
         "    -c, --config CONFIGFILE\n"
         "        Load parameters from a Protobuf text-format file "
         "(.textproto).\n"
         "        Config file values are loaded first, then overridden by any\n"
         "        command-line options specified after --config.\n"
         "\n"
#endif
         "    -R, --resource_trace FILENAME\n"
         "        Write resource usage trace to file.\n"
         "        Output format: time,free_nodes,allocated_nodes\n"
         "        Useful for visualizing resource utilization over time.\n"
         "\n";
  exit(code);
}

void Sim_Params::print() const {
  using std::string;
  using std::to_string;
  string msg;
  msg = "------ Sim params ------\n";
  msg += " - seed: " + to_string(m_seed) + "\n";
  msg += " - max_jobs: " + to_string(m_max_jobs) + "\n";
  msg += " - max_time: " + to_string(m_max_time) + "\n";
  msg += " - sim_start_time: " + to_string(m_sim_start_time) + "\n";
  msg += " - infile: " + m_infile + "\n";
  msg += " - outfile: " + m_outfile + "\n";
  msg += " - redis_uri: " + m_redis_uri + "\n";
  msg += " - redis_key_prefix: " + m_redis_key_prefix + "\n";
  msg += " - total_nodes: " + to_string(m_total_nodes) + "\n";
  msg += " - capacity_schedule: " + m_capacity_schedule + "\n";
  msg += " - num_max_candidates: " + to_string(m_num_max_candidates) + "\n";
  msg += " - job_flush_interval: " + to_string(m_job_flush_interval) + "\n";
  msg += " - is_jobs_set: " + string{m_is_jobs_set ? "true" : "false"} + "\n";
  msg += " - is_time_set: " + string{m_is_time_set ? "true" : "false"} + "\n";

  msg += " - backfill_policy: ";
  if (m_backfill_policy == BackfillPolicy::EASY)
    msg += "EASY";
  else if (m_backfill_policy == BackfillPolicy::CONSERVATIVE)
    msg += "CONSERVATIVE";
  else
    msg += "NONE";
  msg += "\n";

  msg += " - priority_policy: ";
  if (m_priority_policy == PriorityPolicy::FCFS)
    msg += "FCFS";
  else if (m_priority_policy == PriorityPolicy::FCFS_ALT)
    msg += "FCFS_ALT";
  else if (m_priority_policy == PriorityPolicy::FCFS_CONSERVATIVE)
    msg += "FCFS_CONSERVATIVE";
  else if (m_priority_policy == PriorityPolicy::SJF)
    msg += "SJF";
  else
    msg += "LJF";
  msg += "\n";

  msg += " - trace_type: ";
  if (m_trace_type == TraceType::STANDARD)
    msg += "STANDARD";
  else
    msg += "PCON";
  msg += "\n";

  msg += " - trace_format: " + m_trace_format + "\n";
  msg += " - timestamp_format: " + m_timestamp_format + "\n";
  msg += " - timezone: " + m_timezone + "\n";

  msg += " - run_time_mode: ";
  if (m_run_time_mode == RunTimeMode::ACTUAL)
    msg += "ACTUAL";
  else if (m_run_time_mode == RunTimeMode::DISTRIBUTION)
    msg += "DISTRIBUTION";
  else
    msg += "LIMIT";
  msg += "\n";

  msg += " - run_time_distribution: ";
  if (m_run_time_distribution == DistributionType::NORMAL)
    msg += "NORMAL";
  else if (m_run_time_distribution == DistributionType::LOGNORMAL)
    msg += "LOGNORMAL";
  else
    msg += "UNIFORM";
  msg += "\n";

  msg += " - run_time_scale: " + to_string(m_run_time_scale) + "\n";
  msg += " - run_time_stddev: " + to_string(m_run_time_stddev) + "\n";

  std::cout << msg << std::endl;
}

void Sim_Params::set_outfile(const std::string &ofname) {
  m_outfile = ofname;
  if (m_outfile.empty()) {
    if (!m_infile.empty()) {
      m_outfile = dr_evt::get_default_ofname_from_ifname(m_infile);
    } else {
      m_outfile = "sim_out.txt";
    }
  }
}

void Sim_Params::set_infile_list(const std::string &list_path) {
  m_infile_list = list_path;
  m_infile_list_parsed.clear();

  std::ifstream ifs(list_path);
  if (!ifs) {
    throw std::runtime_error("Failed to open --infile_list file: " + list_path);
  }
  std::string line;
  while (std::getline(ifs, line)) {
    // Strip trailing whitespace/carriage returns, same as load()'s
    // own per-row handling - a list file edited on Windows
    // shouldn't silently produce a bad path.
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                             line.back() == '\r' || line.back() == '\n')) {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    m_infile_list_parsed.push_back(line);
  }
  if (m_infile_list_parsed.empty()) {
    throw std::runtime_error("--infile_list file '" + list_path +
                             "' contains no file paths");
  }
  // m_infile needs its first entry right away, since Trace's own
  // constructor (called from Simulation's constructor, before
  // Simulation::run() ever executes) validates a file's header
  // against whatever m_infile holds - list mode still needs a real
  // file for that, same as single-file mode, and this is the only
  // file known yet.
  m_infile = m_infile_list_parsed.front();
}

std::string Sim_Params::get_outfile() const { return m_outfile; }

void Sim_Params::set_resource_trace(const std::string &rfname) {
  m_resource_trace = rfname;
}

std::string Sim_Params::get_resource_trace() const { return m_resource_trace; }

} // end of namespace dr_evt
