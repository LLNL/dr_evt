/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file test_checkpoint_restart.cpp
 * @brief Exact Ser20 checkpoint/restart coverage for streaming simulations.
 */

#define DR_EVT_HAS_CONFIG 1
#include "sim/sim.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

using namespace dr_evt;

namespace {

#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *test_queue = "pbatch";
#else
constexpr const char *test_queue = "1";
#endif

constexpr const char *empty_trace = "/tmp/dr_evt_checkpoint_empty.csv";
constexpr const char *loaded_trace = "/tmp/dr_evt_checkpoint_loaded.csv";
constexpr const char *pcon_trace = "/tmp/dr_evt_checkpoint_pcon.csv";
constexpr size_t scheduler_workload_size = 256;
constexpr sim_time_t scheduler_checkpoint_time = 60.0;
constexpr sim_time_t restarted_workload_start = 160.0;

/** @brief Create the header-only simulation trace required by Trace. */
void write_empty_trace() {
  std::ofstream output(empty_trace);
  output << "job_submit_time,num_nodes,time_limit\n";
  assert(output.good());
}

/** @brief Construct the common deterministic checkpoint test configuration. */
Sim_Params make_params() {
  Sim_Params params;
  params.m_infile = empty_trace;
  params.m_total_nodes = 100;
  params.m_trace_format = "simple";
  params.m_timestamp_format = "epoch";
  params.m_run_time_mode = RunTimeMode::LIMIT;
  return params;
}

/** @brief Compare two floating-point statistics with simulation tolerance. */
bool close(double left, double right) { return std::fabs(left - right) < 1e-9; }

/** @brief Read a complete binary file for byte-for-byte comparison. */
std::string read_file(const std::string &filename) {
  std::ifstream input(filename, std::ios::binary);
  assert(input.good());
  std::ostringstream contents;
  contents << input.rdbuf();
  assert(input.good() || input.eof());
  return contents.str();
}

/** @brief Reconstruct one output from its first archived and active segments.
 */
std::string stitch_first_restart(const std::string &filename) {
  const std::string archive = filename + ".pre-restart.1";
  std::ifstream boundary_input(archive + ".checkpoint-bytes");
  std::uint64_t boundary = 0;
  boundary_input >> boundary;
  assert(boundary_input.good() || boundary_input.eof());
  std::string before = read_file(archive);
  std::string after = read_file(filename);
  const auto header_end = after.find('\n');
  assert(header_end != std::string::npos);
  assert(boundary <= before.size());
  return before.substr(0, static_cast<size_t>(boundary)) +
         after.substr(header_end + 1);
}

/** @brief Populate a large queue with every job lifecycle state represented. */
void populate_checkpoint_workload(Simulation &simulation) {
  simulation.get_trace().load_data(0);
  for (size_t i = 0; i < scheduler_workload_size; ++i) {
    const sim_time_t submit_time = static_cast<sim_time_t>(i / 8) * 5.0;
    const num_nodes_t nodes = static_cast<num_nodes_t>(8 + (i * 7) % 17);
    const tdiff_t run_time = static_cast<tdiff_t>(20 + (i * 11) % 61);
    simulation.append_job(submit_time, nodes, test_queue, run_time);
  }
  simulation.advance_to(scheduler_checkpoint_time);
}

/** @brief Require a checkpoint boundary with a populated mixed-state queue. */
void check_populated_checkpoint_state(const Simulation &simulation) {
  const auto stats = simulation.get_statistics();
  assert(simulation.get_trace().data().size() == scheduler_workload_size);
  assert(stats.jobs_completed > 0);
  assert(stats.jobs_running > 0);
  assert(stats.jobs_waiting > 0);
  const size_t unscheduled = static_cast<size_t>(
      std::count_if(simulation.get_trace().data().begin(),
                    simulation.get_trace().data().end(),
                    [](const Job_Record &job) { return !job.is_scheduled(); }));
  assert(unscheduled > scheduler_workload_size / 2);
  assert(simulation.get_trace().data().back().get_submit_time().first >
         scheduler_checkpoint_time);
}

/** @brief Append the second 256-job workload after the checkpoint boundary. */
void append_restarted_workload(Simulation &simulation) {
  for (size_t i = 0; i < scheduler_workload_size; ++i) {
    const sim_time_t submit_time =
        restarted_workload_start + static_cast<sim_time_t>(i / 8) * 5.0;
    const num_nodes_t nodes = static_cast<num_nodes_t>(8 + ((i + 3) * 5) % 17);
    const tdiff_t run_time = static_cast<tdiff_t>(20 + ((i + 5) * 13) % 61);
    const job_no_t job_id =
        simulation.append_job(submit_time, nodes, test_queue, run_time);
    assert(job_id == static_cast<job_no_t>(scheduler_workload_size + i));
  }
}

/** @brief Verify restart matches uninterrupted execution for one scheduler. */
void test_exact_continuation_case(QueueImplementation queue_impl,
                                  PriorityPolicy priority_policy) {
  auto params = make_params();
  params.m_queue_impl = queue_impl;
  params.m_priority_policy = priority_policy;
  params.m_block_size = 4;

  Simulation uninterrupted(params);
  populate_checkpoint_workload(uninterrupted);
  check_populated_checkpoint_state(uninterrupted);
  const auto checkpoint_stats = uninterrupted.get_statistics();
  const std::vector<job_no_t> status_ids = {0, scheduler_workload_size - 1};
  const auto checkpoint_job_statuses =
      uninterrupted.get_job_statuses(status_ids);

  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  uninterrupted.save_checkpoint(checkpoint);
  append_restarted_workload(uninterrupted);
  uninterrupted.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto expected_stats = uninterrupted.get_statistics();
  assert(expected_stats.jobs_submitted == 2 * scheduler_workload_size);

  checkpoint.seekg(0);
  Simulation restarted(params);
  restarted.load_checkpoint(checkpoint);
  // The checkpoint carries the complete configuration of the run that wrote
  // it, including fields that are not validated on restore.
  assert(restarted.restored_params().has_value());
  assert(restarted.restored_params()->m_total_nodes == params.m_total_nodes);
  assert(restarted.restored_params()->m_seed == params.m_seed);
  assert(restarted.restored_params()->m_max_jobs == params.m_max_jobs);
  assert(restarted.restored_params()->m_trace_format == params.m_trace_format);
  assert(restarted.restored_params()->m_infile == params.m_infile);
  const auto restored_stats = restarted.get_statistics();
  assert(restored_stats.current_time == checkpoint_stats.current_time);
  assert(restored_stats.jobs_completed == checkpoint_stats.jobs_completed);
  assert(restored_stats.jobs_running == checkpoint_stats.jobs_running);
  assert(restored_stats.jobs_waiting == checkpoint_stats.jobs_waiting);
  assert(restored_stats.nodes_in_use == checkpoint_stats.nodes_in_use);
  const auto restored_job_statuses = restarted.get_job_statuses(status_ids);
  assert(restored_job_statuses.size() == checkpoint_job_statuses.size());
  for (size_t i = 0; i < restored_job_statuses.size(); ++i) {
    assert(restored_job_statuses[i].job_idx ==
           checkpoint_job_statuses[i].job_idx);
    assert(restored_job_statuses[i].state == checkpoint_job_statuses[i].state);
    assert(restored_job_statuses[i].start_time ==
           checkpoint_job_statuses[i].start_time);
    assert(restored_job_statuses[i].end_time ==
           checkpoint_job_statuses[i].end_time);
    assert(restored_job_statuses[i].expected_start_time ==
           checkpoint_job_statuses[i].expected_start_time);
  }

  append_restarted_workload(restarted);
  restarted.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto actual_stats = restarted.get_statistics();
  assert(actual_stats.jobs_submitted == expected_stats.jobs_submitted);
  assert(actual_stats.jobs_completed == expected_stats.jobs_completed);
  assert(actual_stats.current_time == expected_stats.current_time);
  assert(actual_stats.nodes_in_use == expected_stats.nodes_in_use);
  assert(close(actual_stats.resource_area, expected_stats.resource_area));
  assert(close(actual_stats.utilization, expected_stats.utilization));
  assert(close(actual_stats.avg_wait_time, expected_stats.avg_wait_time));
  assert(close(actual_stats.avg_turnaround_time,
               expected_stats.avg_turnaround_time));
  assert(actual_stats.makespan == expected_stats.makespan);

  assert(restarted.get_trace().data().size() ==
         uninterrupted.get_trace().data().size());
  for (size_t i = 0; i < restarted.get_trace().data().size(); ++i) {
    const auto &actual = restarted.get_trace().data()[i];
    const auto &expected = uninterrupted.get_trace().data()[i];
    assert(actual.get_begin_time() == expected.get_begin_time());
    assert(actual.get_end_time() == expected.get_end_time());
  }
}

/** @brief Verify exact continuation for every standard scheduler backend. */
void test_exact_continuation() {
  for (const auto queue_impl :
       {QueueImplementation::CIRCULAR, QueueImplementation::DEQUE,
        QueueImplementation::MULTIMAP, QueueImplementation::BLOCK}) {
    test_exact_continuation_case(queue_impl, PriorityPolicy::FCFS);
  }
  for (const auto priority_policy :
       {PriorityPolicy::SJF, PriorityPolicy::LJF}) {
    test_exact_continuation_case(QueueImplementation::CIRCULAR,
                                 priority_policy);
  }
}

/** @brief Verify loaded trace records are not implicitly submitted on load. */
void test_loaded_jobs_remain_unsubmitted() {
  {
    std::ofstream output(loaded_trace);
    output << "job_submit_time,num_nodes,time_limit\n";
    output << "25,10,5\n";
    assert(output.good());
  }
  auto params = make_params();
  params.m_infile = loaded_trace;
  Simulation source(params);
  assert(source.initialize_trace() == 1);
  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  source.save_checkpoint(checkpoint);

  checkpoint.seekg(0);
  Simulation restored(params);
  restored.load_checkpoint(checkpoint);
  const auto before = restored.get_statistics();
  assert(before.jobs_submitted == 0);
  assert(before.jobs_waiting == 0);

  restored.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto after = restored.get_statistics();
  assert(after.jobs_submitted == 0);
  assert(after.jobs_completed == 0);
  assert(restored.get_trace().data().size() == 1);
  assert(!restored.get_trace().data().front().is_scheduled());
}

/** @brief Verify callback-based Custom FCFS resumes with supplied callbacks. */
void test_custom_scheduler_continuation() {
  auto params = make_params();
  params.m_backfill_policy = BackfillPolicy::EASY;
  params.m_num_max_candidates = 4;
  size_t cost_calls = 0;
  const job_cost_function_t cost_function =
      [&cost_calls](job_no_t job, sim_time_t, tdiff_t, num_nodes_t) {
        ++cost_calls;
        return static_cast<job_cost_t>(job);
      };
  const backfill_selector_t selector =
      [](const backfill_candidates_t &candidates) -> std::optional<job_no_t> {
    return candidates.empty() ? std::nullopt
                              : std::optional{candidates.back().first};
  };

  Simulation uninterrupted(params, cost_function, selector);
  populate_checkpoint_workload(uninterrupted);
  check_populated_checkpoint_state(uninterrupted);
  const auto checkpoint_stats = uninterrupted.get_statistics();
  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  uninterrupted.save_checkpoint(checkpoint);
  append_restarted_workload(uninterrupted);
  uninterrupted.advance_to(std::numeric_limits<sim_time_t>::max());
  const auto expected_stats = uninterrupted.get_statistics();
  assert(expected_stats.jobs_submitted == 2 * scheduler_workload_size);

  checkpoint.seekg(0);
  Simulation wrong_scheduler_kind(params);
  bool scheduler_kind_rejected = false;
  try {
    wrong_scheduler_kind.load_checkpoint(checkpoint);
  } catch (const std::runtime_error &) {
    scheduler_kind_rejected = true;
  }
  assert(scheduler_kind_rejected);

  checkpoint.clear();
  checkpoint.seekg(0);
  Simulation restarted(params, cost_function, selector);
  const size_t calls_before_load = cost_calls;
  restarted.load_checkpoint(checkpoint);
  assert(cost_calls == calls_before_load);
  const auto restored_stats = restarted.get_statistics();
  assert(restored_stats.current_time == checkpoint_stats.current_time);
  assert(restored_stats.jobs_completed == checkpoint_stats.jobs_completed);
  assert(restored_stats.jobs_running == checkpoint_stats.jobs_running);
  assert(restored_stats.jobs_waiting == checkpoint_stats.jobs_waiting);
  assert(restored_stats.nodes_in_use == checkpoint_stats.nodes_in_use);
  append_restarted_workload(restarted);
  assert(cost_calls == calls_before_load + scheduler_workload_size);
  restarted.advance_to(std::numeric_limits<sim_time_t>::max());

  const auto &expected = uninterrupted.get_trace().data();
  const auto &actual = restarted.get_trace().data();
  assert(actual.size() == expected.size());
  for (size_t i = 0; i < actual.size(); ++i) {
    assert(actual[i].get_begin_time() == expected[i].get_begin_time());
    assert(actual[i].get_end_time() == expected[i].get_end_time());
  }
  const auto actual_stats = restarted.get_statistics();
  assert(actual_stats.jobs_submitted == expected_stats.jobs_submitted);
  assert(actual_stats.jobs_completed == expected_stats.jobs_completed);
  assert(actual_stats.current_time == expected_stats.current_time);
  assert(actual_stats.nodes_in_use == expected_stats.nodes_in_use);
  assert(close(actual_stats.resource_area, expected_stats.resource_area));
  assert(close(actual_stats.utilization, expected_stats.utilization));
  assert(close(actual_stats.avg_wait_time, expected_stats.avg_wait_time));
  assert(close(actual_stats.avg_turnaround_time,
               expected_stats.avg_turnaround_time));
  assert(actual_stats.makespan == expected_stats.makespan);
}

/** @brief Verify reclaimed jobs and incremental outputs continue exactly once.
 */
void test_output_continuation() {
  const std::string checkpoint_path = "/tmp/dr_evt_checkpoint.bin";
  const std::string baseline_job_output =
      "/tmp/dr_evt_checkpoint_baseline_jobs.csv";
  const std::string baseline_resource_output =
      "/tmp/dr_evt_checkpoint_baseline_resources.csv";
  const std::string restarted_job_output =
      "/tmp/dr_evt_checkpoint_restarted_jobs.csv";
  const std::string restarted_resource_output =
      "/tmp/dr_evt_checkpoint_restarted_resources.csv";
  std::remove(checkpoint_path.c_str());
  std::remove(baseline_job_output.c_str());
  std::remove(baseline_resource_output.c_str());
  std::remove(restarted_job_output.c_str());
  std::remove(restarted_resource_output.c_str());
  std::remove((restarted_job_output + ".pre-restart.1").c_str());
  std::remove(
      (restarted_job_output + ".pre-restart.1.checkpoint-bytes").c_str());
  std::remove((restarted_resource_output + ".pre-restart.1").c_str());
  std::remove(
      (restarted_resource_output + ".pre-restart.1.checkpoint-bytes").c_str());

  auto baseline_params = make_params();
  baseline_params.set_outfile(baseline_job_output);
  baseline_params.set_resource_trace(baseline_resource_output);
  {
    Simulation baseline(baseline_params);
    baseline.get_trace().load_data(0);
    baseline.append_job(0.0, 50, test_queue, 10.0);
    baseline.append_job(0.0, 50, test_queue, 30.0);
    baseline.advance_to(15.0);
    baseline.flush_completed_jobs();
    baseline.advance_to(std::numeric_limits<sim_time_t>::max());
    baseline.write_simulated_trace();
    baseline.write_resource_trace(baseline_resource_output);
    assert(baseline.get_statistics().jobs_completed == 2);
  }

  auto restarted_params = make_params();
  restarted_params.set_outfile(restarted_job_output);
  restarted_params.set_resource_trace(restarted_resource_output);
  {
    Simulation source(restarted_params);
    source.get_trace().load_data(0);
    source.append_job(0.0, 50, test_queue, 10.0);
    source.append_job(0.0, 50, test_queue, 30.0);
    source.advance_to(15.0);
    source.flush_completed_jobs();
    assert(source.get_trace().num_reclaimed() == 1);
    source.save_checkpoint(checkpoint_path);
    // Produce stale post-checkpoint rows. Restart must preserve this file as
    // an archive while the stitch boundary excludes these rows.
    source.advance_to(std::numeric_limits<sim_time_t>::max());
    source.write_simulated_trace();
    source.write_resource_trace(restarted_resource_output);
  }
  {
    Simulation resumed(restarted_params);
    resumed.load_checkpoint(checkpoint_path);
    resumed.advance_to(std::numeric_limits<sim_time_t>::max());
    resumed.write_simulated_trace();
    resumed.write_resource_trace(restarted_resource_output);
    assert(resumed.get_statistics().jobs_completed == 2);
  }

  assert(stitch_first_restart(restarted_job_output) ==
         read_file(baseline_job_output));
  assert(stitch_first_restart(restarted_resource_output) ==
         read_file(baseline_resource_output));

  std::istringstream jobs(stitch_first_restart(restarted_job_output));
  std::string line;
  size_t lines = 0;
  while (std::getline(jobs, line)) {
    ++lines;
  }
  assert(lines == 3); // one header and two non-duplicated job rows
}

/** @brief Compare progressive file-boundary restart with one uninterrupted run. */
void test_progressive_output_continuation() {
  const std::string first = "/tmp/dr_evt_checkpoint_progressive_1.csv";
  const std::string second = "/tmp/dr_evt_checkpoint_progressive_2.csv";
  const std::string checkpoint = "/tmp/dr_evt_checkpoint_progressive.bin";
  const std::string baseline_jobs = "/tmp/dr_evt_progressive_baseline_jobs.csv";
  const std::string baseline_resources =
      "/tmp/dr_evt_progressive_baseline_resources.csv";
  const std::string restart_jobs = "/tmp/dr_evt_progressive_restart_jobs.csv";
  const std::string restart_resources =
      "/tmp/dr_evt_progressive_restart_resources.csv";
  {
    std::ofstream output(first);
    output << "job_submit_time,num_nodes,time_limit\n0,50,5\n10,50,20\n";
  }
  {
    std::ofstream output(second);
    output << "job_submit_time,num_nodes,time_limit\n20,100,5\n30,20,10\n";
  }
  for (const auto &path : {checkpoint, baseline_jobs, baseline_resources,
                           restart_jobs, restart_resources}) {
    std::remove(path.c_str());
  }
  std::remove((restart_jobs + ".pre-restart.1").c_str());
  std::remove((restart_jobs + ".pre-restart.1.checkpoint-bytes").c_str());
  std::remove((restart_resources + ".pre-restart.1").c_str());
  std::remove(
      (restart_resources + ".pre-restart.1.checkpoint-bytes").c_str());

  auto progressive_params = [&](const std::string &jobs,
                                const std::string &resources) {
    auto params = make_params();
    params.m_infile = first;
    params.m_infile_list = "test-list";
    params.m_infile_list_parsed = {first, second};
    params.set_outfile(jobs);
    params.set_resource_trace(resources);
    return params;
  };
  {
    auto params = progressive_params(baseline_jobs, baseline_resources);
    Simulation baseline(params);
    baseline.run();
    baseline.write_simulated_trace();
    baseline.write_resource_trace(baseline_resources);
  }
  {
    auto params = progressive_params(restart_jobs, restart_resources);
    params.m_checkpoint_file = checkpoint;
    params.m_checkpoint_interval_jobs = 1;
    params.m_is_time_set = true;
    params.m_max_time = 10.0;
    Simulation source(params);
    source.run();
    const std::string boundary_checkpoint = read_file(checkpoint);
    assert(!boundary_checkpoint.empty());
    source.advance_to(std::numeric_limits<sim_time_t>::max());
    source.write_simulated_trace();
    source.write_resource_trace(restart_resources);
    std::ofstream restore_checkpoint(checkpoint,
                                     std::ios::binary | std::ios::trunc);
    restore_checkpoint.write(boundary_checkpoint.data(),
                             boundary_checkpoint.size());
    assert(restore_checkpoint.good());
  }
  {
    auto params = progressive_params(restart_jobs, restart_resources);
    params.m_checkpoint_file = checkpoint;
    params.m_checkpoint_interval_jobs = 1;
    Simulation resumed(params);
    resumed.load_checkpoint(checkpoint);
    resumed.run();
    resumed.write_simulated_trace();
    resumed.write_resource_trace(restart_resources);
  }
  assert(stitch_first_restart(restart_jobs) == read_file(baseline_jobs));
  assert(stitch_first_restart(restart_resources) ==
         read_file(baseline_resources));
}

#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
/** @brief Compare stitched Redis restart output with an uninterrupted run. */
void test_redis_output_continuation() {
  const char *uri_value = std::getenv("DR_EVT_TEST_REDIS_URI");
  const char *prefix_value = std::getenv("DR_EVT_TEST_REDIS_PREFIX");
  if (uri_value == nullptr || prefix_value == nullptr) {
    return;
  }
  const std::string uri(uri_value);
  const std::string base(prefix_value);
  const std::string baseline_prefix = base + ":checkpoint-baseline";
  const std::string restart_prefix = base + ":checkpoint-restart";
  const std::string checkpoint = "/tmp/dr_evt_redis_checkpoint.bin";

  auto redis_params = [&](const std::string &prefix) {
    auto params = make_params();
    params.set_outfile("unused.csv");
    params.set_resource_trace("unused-resources.csv");
    params.m_redis_uri = uri;
    params.m_redis_key_prefix = prefix;
    params.m_job_flush_interval = 1;
    return params;
  };
  auto populate = [](Simulation &simulation) {
    simulation.get_trace().load_data(0);
    simulation.append_job(0.0, 50, test_queue, 10.0);
    simulation.append_job(0.0, 50, test_queue, 30.0);
  };
  {
    auto params = redis_params(baseline_prefix);
    Simulation baseline(params);
    populate(baseline);
    baseline.advance_to(std::numeric_limits<sim_time_t>::max());
    baseline.write_simulated_trace();
    baseline.write_resource_trace("unused-resources.csv");
  }
  {
    auto params = redis_params(restart_prefix);
    Simulation source(params);
    populate(source);
    source.advance_to(15.0);
    source.flush_completed_jobs();
    source.save_checkpoint(checkpoint);
    source.advance_to(std::numeric_limits<sim_time_t>::max());
    source.write_simulated_trace();
    source.write_resource_trace("unused-resources.csv");
  }
  {
    auto params = redis_params(restart_prefix);
    Simulation resumed(params);
    resumed.load_checkpoint(checkpoint);
    resumed.advance_to(std::numeric_limits<sim_time_t>::max());
    resumed.write_simulated_trace();
    resumed.write_resource_trace("unused-resources.csv");
  }

  const std::string command =
      "python3 scripts/stitch_checkpoint_output.py --redis-uri '" + uri +
      "' --redis-prefix '" + restart_prefix + "'";
  assert(std::system(command.c_str()) == 0);
  const std::string baseline_jobs = "/tmp/dr_evt_redis_baseline_jobs.csv";
  const std::string restarted_jobs = "/tmp/dr_evt_redis_restarted_jobs.csv";
  const std::string baseline_resources =
      "/tmp/dr_evt_redis_baseline_resources.csv";
  const std::string restarted_resources =
      "/tmp/dr_evt_redis_restarted_resources.csv";
  const auto export_key = [&](const std::string &key,
                              const std::string &path) {
    const std::string export_command = "redis-cli -u '" + uri +
                                       "' --raw GET '" + key + "' > '" +
                                       path + "'";
    assert(std::system(export_command.c_str()) == 0);
  };
  export_key(baseline_prefix + ":csv", baseline_jobs);
  export_key(restart_prefix + ":csv", restarted_jobs);
  export_key(baseline_prefix + ":resources:csv", baseline_resources);
  export_key(restart_prefix + ":resources:csv", restarted_resources);
  assert(read_file(baseline_jobs) == read_file(restarted_jobs));
  assert(read_file(baseline_resources) == read_file(restarted_resources));
}
#endif

/** @brief Verify loading with incompatible configuration fails. */
void test_rejected_checkpoints() {
  auto params = make_params();
  Simulation source(params);
  populate_checkpoint_workload(source);
  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  source.save_checkpoint(checkpoint);

  auto incompatible_params = make_params();
  incompatible_params.m_total_nodes = 99;
  Simulation incompatible(incompatible_params);
  checkpoint.seekg(0);
  bool mismatch_rejected = false;
  try {
    incompatible.load_checkpoint(checkpoint);
  } catch (const std::runtime_error &) {
    mismatch_rejected = true;
  }
  assert(mismatch_rejected);
}

/** @brief Verify Pcon records, live totals, and resource output survive
 * restart. */
void test_pcon_continuation() {
  const std::string checkpoint_path = "/tmp/dr_evt_pcon_checkpoint.bin";
  const std::string baseline_resource_output =
      "/tmp/dr_evt_pcon_checkpoint_baseline_resources.csv";
  const std::string restarted_resource_output =
      "/tmp/dr_evt_pcon_checkpoint_restarted_resources.csv";
  std::remove(checkpoint_path.c_str());
  std::remove(baseline_resource_output.c_str());
  std::remove(restarted_resource_output.c_str());
  std::remove((restarted_resource_output + ".pre-restart.1").c_str());
  std::remove(
      (restarted_resource_output + ".pre-restart.1.checkpoint-bytes").c_str());

  {
    std::ofstream input(pcon_trace);
    input << "job_submit_time,num_nodes,q_id,time_limit,avgpcon,minpcon,"
             "maxpcon\n"
          << "0,3,1,10,1.5,2,3\n"
          << "0,2,1,4,0.5,1,1.5\n"
          << "0,1,1,8,2.5,3,4\n";
    assert(input.good());
  }

  auto make_pcon_params = [](const std::string &resource_output) {
    Sim_Params params;
    params.m_infile = pcon_trace;
    params.m_trace_type = TraceType::PCON;
    params.m_total_nodes = 4;
    params.m_trace_format = "simple";
    params.m_timestamp_format = "epoch";
    params.m_run_time_mode = RunTimeMode::LIMIT;
    params.set_resource_trace(resource_output);
    return params;
  };

  {
    auto params = make_pcon_params(baseline_resource_output);
    PconSimulation baseline(params);
    baseline.run();
    baseline.write_resource_trace(baseline_resource_output);
  }

  auto restart_params = make_pcon_params(restarted_resource_output);
  restart_params.m_is_time_set = true;
  restart_params.m_max_time = 5.0;
  {
    PconSimulation source(restart_params);
    source.run();
    const auto stats = source.get_statistics();
    assert(stats.jobs_running > 0);
    assert(stats.jobs_waiting > 0);
    source.save_checkpoint(checkpoint_path);
  }

  {
    PconSimulation resumed(restart_params);
    resumed.load_checkpoint(checkpoint_path);
    const auto &jobs = resumed.get_trace().data();
    assert(jobs.size() == 3);
    assert(close(jobs[0].pcon().avgpcon, 1.5));
    assert(close(jobs[1].pcon().minpcon, 1.0));
    assert(close(jobs[2].pcon().maxpcon, 4.0));
    resumed.advance_to(std::numeric_limits<sim_time_t>::max());
    resumed.write_resource_trace(restarted_resource_output);
    assert(resumed.get_statistics().jobs_completed == 3);
  }

  assert(stitch_first_restart(restarted_resource_output) ==
         read_file(baseline_resource_output));

  {
    Simulation wrong_trace_type(restart_params);
    bool trace_type_rejected = false;
    try {
      wrong_trace_type.load_checkpoint(checkpoint_path);
    } catch (const std::runtime_error &) {
      trace_type_rejected = true;
    }
    assert(trace_type_rejected);
  }

  std::remove(checkpoint_path.c_str());
  std::remove(baseline_resource_output.c_str());
  std::remove(restarted_resource_output.c_str());
  std::remove(pcon_trace);
}

} // namespace

/** @brief Run all exact checkpoint/restart tests. */
int main() {
  write_empty_trace();
  test_exact_continuation();
  test_loaded_jobs_remain_unsubmitted();
  test_custom_scheduler_continuation();
  test_output_continuation();
  test_progressive_output_continuation();
#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
  test_redis_output_continuation();
#endif
  test_rejected_checkpoints();
  test_pcon_continuation();
  std::cout << "Checkpoint/restart tests passed\n";
  return EXIT_SUCCESS;
}
