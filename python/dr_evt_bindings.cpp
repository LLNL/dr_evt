/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/**
 * @file dr_evt_bindings.cpp
 * @brief Defines the ``dr_evt`` Python extension module with pybind11.
 *
 * @details Exposes the simulation configuration, policy enumerations,
 * streaming controls, state-monitoring values, and trace output facilities.
 * Python numeric values map to the corresponding DR_EVT simulation types;
 * times are expressed in simulation-time units and node counts are integers.
 */

#include "dr_evt_config.hpp"
#include "params/sim_params.hpp"
#include "sim/sim.hpp"
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace dr_evt;

/** @brief Initialize the `dr_evt` Python extension module.
 * @details The pybind11-generated module handle is populated with DR_EVT
 * types and functions. Doxygen does not expose that macro argument as a
 * function parameter. */
PYBIND11_MODULE(dr_evt, m) {
  m.doc() = "DR_EVT: HPC Job Scheduler Simulator - Python API";
  m.attr("legacy_queue_input") = py::bool_(DR_EVT_LEGACY_QUEUE_INPUT != 0);

  // Scheduler policies exported as Python enum classes.
  py::enum_<RunTimeMode>(m, "RunTimeMode",
                         "Controls how a job's actual run time is selected; "
                         "the scheduler uses its time limit for planning.")
      .value("ACTUAL", RunTimeMode::ACTUAL)
      .value("DISTRIBUTION", RunTimeMode::DISTRIBUTION)
      .value("LIMIT", RunTimeMode::LIMIT)
      .export_values();

  py::enum_<BackfillPolicy>(
      m, "BackfillPolicy",
      "Selects no backfilling, EASY backfilling, or conservative backfilling.")
      .value("NONE", BackfillPolicy::NONE)
      .value("EASY", BackfillPolicy::EASY)
      .value("CONSERVATIVE", BackfillPolicy::CONSERVATIVE)
      .export_values();

  py::enum_<PriorityPolicy>(m, "PriorityPolicy",
                            "Selects the waiting-job priority policy.")
      .value("FCFS", PriorityPolicy::FCFS)
      .value("FCFS_CONSERVATIVE", PriorityPolicy::FCFS_CONSERVATIVE)
      .value("SJF", PriorityPolicy::SJF)
      .value("LJF", PriorityPolicy::LJF)
      .export_values();

  py::enum_<Simulation::Job_State>(m, "JobState",
                                   "Lifecycle state of an appended job.")
      .value("PENDING", Simulation::Job_State::PENDING)
      .value("RUNNING", Simulation::Job_State::RUNNING)
      .value("COMPLETED", Simulation::Job_State::COMPLETED)
      .value("REJECTED", Simulation::Job_State::REJECTED);

  // Mutable configuration populated before constructing Simulation.
  py::class_<Sim_Params>(m, "SimParams")
      .def(py::init<>(), "Creates a configuration with DR_EVT default values.")
      .def_readwrite("infile", &Sim_Params::m_infile,
                     "str: Path to the input trace used by batch mode and "
                     "initialize_trace().")
      .def_readwrite("redis_uri", &Sim_Params::m_redis_uri,
                     "str: Redis connection URI; empty selects file output.")
      .def_readwrite("redis_key_prefix", &Sim_Params::m_redis_key_prefix,
                     "str: Redis namespace for output and search indexes.")
      .def_readwrite("checkpoint_file", &Sim_Params::m_checkpoint_file,
                     "str: Automatic binary checkpoint destination.")
      .def_readwrite("checkpoint_interval_jobs",
                     &Sim_Params::m_checkpoint_interval_jobs,
                     "int: Completed-job cadence for automatic checkpoints.")
      .def_readwrite("total_nodes", &Sim_Params::m_total_nodes,
                     "int: Total scheduler-managed compute nodes.")
      .def_readwrite("capacity_schedule", &Sim_Params::m_capacity_schedule,
                     "str: Optional capacity-change CSV path.")
      .def_readwrite("sim_start_time", &Sim_Params::m_sim_start_time,
                     "float: Global simulation start time; a positive value "
                     "enables replay-based warm start for replay input.")
      .def_readwrite("trace_format", &Sim_Params::m_trace_format,
                     "str: Input trace format identifier.")
      .def_readwrite("timestamp_format", &Sim_Params::m_timestamp_format,
                     "str: Timestamp format used by the input trace.")
      .def_readwrite("run_time_mode", &Sim_Params::m_run_time_mode,
                     "RunTimeMode: Policy for selecting actual job duration.")
      .def_readwrite(
          "backfill_policy", &Sim_Params::m_backfill_policy,
          "BackfillPolicy: Backfilling policy used by the scheduler.")
      .def_readwrite("num_max_candidates", &Sim_Params::m_num_max_candidates,
                     "int: Maximum feasible jobs offered to the experimental "
                     "backfill selector.")
      .def_readwrite("priority_policy", &Sim_Params::m_priority_policy,
                     "PriorityPolicy: Waiting-job ordering policy.")
      .def_readwrite("verbose", &Sim_Params::m_verbose,
                     "bool: Enables verbose simulator output.");

  // One value passed to Simulation.append_jobs().
  py::class_<Simulation::Job_Append_Request>(m, "JobAppendRequest")
      .def(py::init([](sim_time_t submit_time, num_nodes_t num_nodes,
                       const std::string &queue, tdiff_t limit_time,
                       std::optional<tdiff_t> actual_run_time) {
             return Simulation::Job_Append_Request{submit_time, num_nodes,
                                                   queue, limit_time,
                                                   actual_run_time};
           }),
           py::arg("submit_time"), py::arg("num_nodes"), py::arg("queue"),
           py::arg("limit_time"), py::arg("actual_run_time") = py::none(),
           "Create one batch job request. submit_time is a float, num_nodes is "
           "an int, "
           "queue is a str, limit_time is a float, and actual_run_time is an "
           "optional float.")
      .def_readwrite("submit_time",
                     &Simulation::Job_Append_Request::submit_time,
                     "float: Arrival time, not earlier than the simulation's "
                     "current time.")
      .def_readwrite("num_nodes", &Simulation::Job_Append_Request::num_nodes,
                     "int: Requested node count.")
      .def_readwrite("queue", &Simulation::Job_Append_Request::queue,
                     "str: Numeric queue ID, or a name in legacy-input builds.")
      .def_readwrite("limit_time", &Simulation::Job_Append_Request::limit_time,
                     "float: Requested wall-time limit.")
      .def_readwrite("actual_run_time",
                     &Simulation::Job_Append_Request::actual_run_time,
                     "Optional[float]: Known execution time.");

  // Statistics structure
  py::class_<Simulation::Statistics>(m, "Statistics")
      .def_readonly("jobs_submitted", &Simulation::Statistics::jobs_submitted,
                    "int: Jobs accepted into the wait queue.")
      .def_readonly("jobs_started", &Simulation::Statistics::jobs_started,
                    "int: Jobs that have begun execution.")
      .def_readonly("jobs_completed", &Simulation::Statistics::jobs_completed,
                    "int: Number of completed jobs.")
      .def_readonly("jobs_running", &Simulation::Statistics::jobs_running,
                    "int: Number of running jobs.")
      .def_readonly("jobs_waiting", &Simulation::Statistics::jobs_waiting,
                    "int: Number of waiting jobs.")
      .def_readonly("current_time", &Simulation::Statistics::current_time,
                    "float: Current simulation time.")
      .def_readonly("total_nodes", &Simulation::Statistics::total_nodes,
                    "int: Configured node capacity.")
      .def_readonly("nodes_in_use", &Simulation::Statistics::nodes_in_use,
                    "int: Nodes allocated to running jobs.")
      .def_readonly("nodes_available", &Simulation::Statistics::nodes_available,
                    "int: Currently unallocated nodes.")
      .def_readonly("resource_area", &Simulation::Statistics::resource_area,
                    "float: Live time-integrated allocation for Custom FCFS; "
                    "scheduled-job area for standard schedulers.")
      .def_readonly("utilization", &Simulation::Statistics::utilization,
                    "float: Live time-accounted utilization for Custom FCFS; "
                    "post-hoc schedule utilization otherwise.")
      .def_readonly("avg_wait_time", &Simulation::Statistics::avg_wait_time,
                    "float: Mean completed-job wait time.")
      .def_readonly("avg_run_time", &Simulation::Statistics::avg_run_time,
                    "float: Mean completed-job execution time.")
      .def_readonly("avg_turnaround_time",
                    &Simulation::Statistics::avg_turnaround_time,
                    "float: Mean completed-job turnaround time.")
      .def_readonly("avg_bounded_slowdown",
                    &Simulation::Statistics::avg_bounded_slowdown,
                    "float: Mean bounded slowdown using a 10-second bound.")
      .def_readonly("makespan", &Simulation::Statistics::makespan,
                    "float: Time from first submission to final completion.")
      .def("__repr__", [](const Simulation::Statistics &s) {
        return "Statistics(jobs=" + std::to_string(s.jobs_completed) + "/" +
               std::to_string(s.jobs_started) + "/" +
               std::to_string(s.jobs_submitted) +
               ", utilization=" + std::to_string(s.utilization * 100) + "%)";
      });

  // FCFS/EASY reservation snapshot for evaluating in-process backfill
  // candidates.
  py::class_<Simulation::Backfill_Window::Resource_Release>(m,
                                                            "ResourceRelease")
      .def_readonly("time",
                    &Simulation::Backfill_Window::Resource_Release::time,
                    "float: Simulation time at which nodes become available.")
      .def_readonly(
          "nodes_released",
          &Simulation::Backfill_Window::Resource_Release::nodes_released,
          "int: Number of nodes released at time.");

  py::class_<Simulation::Backfill_Window>(m, "BackfillWindow")
      .def_readonly("current_time", &Simulation::Backfill_Window::current_time,
                    "float: Simulation time represented by this snapshot.")
      .def_readonly("available_nodes",
                    &Simulation::Backfill_Window::available_nodes,
                    "int: Nodes immediately available at current_time.")
      .def_readonly(
          "shadow_time", &Simulation::Backfill_Window::shadow_time,
          "Earliest FCFS-head start time, or -1 when no job is waiting")
      .def_readonly("releases", &Simulation::Backfill_Window::releases,
                    "list[ResourceRelease]: Future resource-release events in "
                    "time order.");

  py::class_<Simulation::Job_Status>(m, "JobStatus")
      .def_readonly("job_idx", &Simulation::Job_Status::job_idx,
                    "int: Stable identifier returned by append_job(s).")
      .def_readonly("state", &Simulation::Job_Status::state,
                    "JobState: Current lifecycle state.")
      .def_readonly("start_time", &Simulation::Job_Status::start_time,
                    "Optional[float]: Scheduled start time.")
      .def_readonly("end_time", &Simulation::Job_Status::end_time,
                    "Optional[float]: Scheduled completion time.")
      .def_readonly("expected_start_time",
                    &Simulation::Job_Status::expected_start_time,
                    "Optional[float]: Current projection while pending.");

  // Main Simulation class
  py::class_<Simulation>(m, "Simulation")
      .def(py::init<const Sim_Params &>(), py::arg("params"),
           "Create a simulation from SimParams. The configuration is copied.")
      .def(py::init<const Sim_Params &, job_cost_function_t,
                    backfill_selector_t>(),
           py::arg("params"), py::arg("job_cost_function"),
           py::arg("backfill_selector"),
           "Create an experimental circular-buffer simulation. One callback "
           "computes each job's cost at insertion; the other selects one "
           "feasible (job_id, cost) backfill candidate.")

      // Batch mode
      .def("run", &Simulation::run,
           "Run batch simulation to completion. Returns None.")

      // Streaming API - New jobs are appended and enqueued atomically.
      .def("append_job", &Simulation::append_job, py::arg("submit_time"),
           py::arg("num_nodes"), py::arg("queue"), py::arg("limit_time"),
           py::arg("actual_run_time") = py::none(),
           "Append and enqueue one new job.\n\n"
           "Args:\n"
           "    submit_time (float): Arrival time, not earlier than current "
           "time.\n"
           "    num_nodes (int): Requested node count.\n"
           "    queue (str): Numeric queue ID, or a name in legacy-input "
           "builds.\n"
           "    limit_time (float): Requested wall-time limit.\n"
           "    actual_run_time (Optional[float]): Known execution time.\n"
           "Returns:\n"
           "    int: Identifier of the appended trace job.")

      .def("append_jobs", &Simulation::append_jobs, py::arg("requests"),
           "Append and enqueue multiple new jobs atomically.\n\n"
           "Args:\n"
           "    requests (Sequence[JobAppendRequest]): New jobs in "
           "non-decreasing "
           "submit_time order.\n"
           "Returns:\n"
           "    list[int]: Appended job identifiers in the same order as "
           "requests.\n\n"
           "Raises:\n"
           "    RuntimeError: If validation or capacity handling rejects the "
           "batch; "
           "no request is appended.")

      .def("get_job_statuses", &Simulation::get_job_statuses,
           py::arg("job_idxs"),
           "Return status snapshots for appended job IDs in request order. "
           "Pending jobs have expected_start_time; scheduled jobs have "
           "start_time and end_time.")

      // Streaming API - Time advancement
      .def("run_until_exclusive", &Simulation::run_until_exclusive,
           py::arg("target_time"),
           "Advance through events strictly before target_time.\n\n"
           "Args:\n    target_time (float): Exclusive time bound.\n"
           "Returns:\n    None")

      .def("advance_to", &Simulation::advance_to, py::arg("target_time"),
           "Advance through events at or before target_time.\n\n"
           "Args:\n    target_time (float): Inclusive time bound.\n"
           "Returns:\n    None")

#if defined(DR_EVT_HAS_SER20)
      .def(
          "save_checkpoint",
          [](Simulation &simulation, const std::string &filename) {
            simulation.save_checkpoint(filename);
          },
          py::arg("filename"),
          "Save the complete simulation state to a binary checkpoint file.")

      .def(
          "load_checkpoint",
          [](Simulation &simulation, const std::string &filename) {
            simulation.load_checkpoint(filename);
          },
          py::arg("filename"),
          "Replace current state from a compatible checkpoint file.")
#endif

      // Monitoring - Basic state
      .def("get_current_time", &Simulation::get_current_time,
           "Return the current simulation time as float.")

      .def("get_nodes_in_use", &Simulation::get_nodes_in_use,
           "Return the number of allocated nodes as int.")

      .def("get_current_utilization", &Simulation::get_current_utilization,
           "Return current nodes-in-use divided by total nodes as float.")

      .def("get_resource_area", &Simulation::get_resource_area,
           "Return Custom-FCFS time-integrated allocation in node-seconds. "
           "Raises RuntimeError for other scheduler implementations.")

      .def("get_available_nodes", &Simulation::get_available_nodes,
           "Return the number of unallocated nodes as int.")

      // Monitoring - Queue status
      .def("get_active_job_count", &Simulation::get_active_job_count,
           "Return the number of arrived, waiting jobs as int.")

      .def("get_fcfs_head_shadow_time", &Simulation::get_fcfs_head_shadow_time,
           "Return the FCFS-head predicted start time as float, or -1.0 if no "
           "job waits.")

      .def("get_backfill_window", &Simulation::get_backfill_window,
           "Return a BackfillWindow snapshot for evaluating a backfill "
           "candidate, including all running-job releases.")

      .def("get_prediction_horizon", &Simulation::get_prediction_horizon,
           py::arg("utilization"),
           "Estimate the waiting-queue drain time from the FCFS shadow time. "
           "Computed on demand for supported EASY schedulers; future arrivals "
           "are excluded.")

      // Monitoring - Comprehensive statistics
      .def("get_statistics", &Simulation::get_statistics,
           "Return a read-only Statistics snapshot.")

      // Output
      .def("write_simulated_trace", &Simulation::write_simulated_trace,
           "Write the simulated trace to the configured output. Returns None.")

      .def(
          "print_stats",
          [](const Simulation &sim) { sim.print_stats(std::cout); },
          "Print simulation statistics to standard output. Returns None.")

      // Trace access
      .def(
          "get_trace_size",
          [](const Simulation &sim) { return sim.get_trace().data().size(); },
          "Return the number of jobs currently stored in the trace as int.")

      .def(
          "initialize_trace",
          [](Simulation &sim, size_t max_jobs) {
            return sim.initialize_trace(static_cast<num_jobs_t>(max_jobs));
          },
          py::arg("max_jobs") = 0,
          "Load up to max_jobs jobs from the configured trace file.\n\n"
          "Args:\n    max_jobs (int): Maximum jobs to load; 0 loads all jobs.\n"
          "Returns:\n    int: Number of jobs loaded.\n\n"
          "Call before streaming when jobs originate in a trace file.");

  // Module-level version info
  m.attr("__version__") = "1.0.0";
}
