/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

/** @file dr_evt_client.cpp
 * @brief Command-line client for the DR_EVT protobuf simulation service.
 */

#include <fstream>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "dr_evt_config.hpp"

#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
#include <sw/redis++/redis++.h>
#endif

#include "dr_evt_service.grpc.pb.h"

using dr_evt_grpc::ClientMessage;
using dr_evt_grpc::ServerMessage;
using dr_evt_grpc::SimulationService;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;

namespace {
#if DR_EVT_LEGACY_QUEUE_INPUT
constexpr const char *kDefaultQueueInput = "pbatch";
#else
constexpr const char *kDefaultQueueInput =
    "1"; ///< Numeric ID of the default Queue1.
#endif

struct ClientOptions {
  std::string server_address;
  std::string job_data_file;
  std::string redis_uri;
  std::string redis_key_prefix;
  double advance_to = 0.0;
  bool query_after_advance = false;
};

/**
 * @brief Print command-line syntax for the example client.
 * @param[in] program_name Executable name shown in the usage text.
 */
void print_usage(const char *program_name) {
  std::cerr
      << "Usage: " << program_name
      << " <server_address> <job_data_file> [options]\n"
      << "  e.g., " << program_name
      << " localhost:50051 /path/to/jobs.csv\n"
      << "  Redis status example:\n    " << program_name
      << " localhost:50051 /path/to/jobs.csv"
         " --redis-uri redis://127.0.0.1:6379"
         " --redis-key-prefix dr_evt:client-example --advance-to 100\n"
      << "Options:\n"
      << "  --redis-uri URI          Redis server used for finalized jobs\n"
      << "  --redis-key-prefix NAME  Redis output namespace\n"
      << "  --advance-to TIME        Advance, query statuses, then finish\n"
      << "  <job_data_file>: queue-free simple-format CSV\n"
      << "  (job_submit_time,num_nodes,time_limit). The server validates its\n"
      << "  header but never loads its job rows.\n";
}

/**
 * @brief Parse the example client's positional and named arguments.
 * @param[in] argc Command-line argument count.
 * @param[in] argv Command-line argument vector.
 * @return Validated client configuration.
 * @throws std::invalid_argument If an option is unknown, lacks a value, has
 * an invalid value, or Redis configuration is incomplete.
 * @throws std::out_of_range If the advance time is outside double's range.
 */
ClientOptions parse_options(int argc, char **argv) {
  if (argc < 3) {
    throw std::invalid_argument("server address and job data file are required");
  }

  ClientOptions options;
  options.server_address = argv[1];
  options.job_data_file = argv[2];
  for (int i = 3; i < argc; ++i) {
    const std::string_view option(argv[i]);
    if (option != "--redis-uri" && option != "--redis-key-prefix" &&
        option != "--advance-to") {
      throw std::invalid_argument("unknown option: " + std::string(option));
    }
    if (++i >= argc) {
      throw std::invalid_argument("missing value for " + std::string(option));
    }
    if (option == "--redis-uri") {
      options.redis_uri = argv[i];
    } else if (option == "--redis-key-prefix") {
      options.redis_key_prefix = argv[i];
    } else {
      std::size_t parsed = 0;
      options.advance_to = std::stod(argv[i], &parsed);
      if (parsed != std::string_view(argv[i]).size()) {
        throw std::invalid_argument("invalid --advance-to value: " +
                                    std::string(argv[i]));
      }
      options.query_after_advance = true;
    }
  }

  if (options.redis_uri.empty() != options.redis_key_prefix.empty()) {
    throw std::invalid_argument(
        "--redis-uri and --redis-key-prefix must be specified together");
  }
  if (!options.redis_uri.empty() && !options.query_after_advance) {
    throw std::invalid_argument(
        "--advance-to is required when Redis status lookup is enabled");
  }
#if !defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
  if (!options.redis_uri.empty()) {
    throw std::invalid_argument(
        "this client was built without Redis support; reconfigure with "
        "-DDR_EVT_WITH_REDIS=ON");
  }
#endif
  return options;
}

/**
 * @brief Return the printable name of a protobuf job state.
 * @param[in] state State returned by GetJobStatusesRequest.
 * @return Stable lowercase state name.
 */
const char *job_state_name(dr_evt_grpc::JobState state) {
  switch (state) {
  case dr_evt_grpc::JOB_STATE_PENDING:
    return "pending";
  case dr_evt_grpc::JOB_STATE_RUNNING:
    return "running";
  case dr_evt_grpc::JOB_STATE_COMPLETED:
    return "completed";
  case dr_evt_grpc::JOB_STATE_REJECTED:
    return "rejected";
  default:
    return "unspecified";
  }
}

/**
 * @brief Print a live job status returned by the simulation server.
 * @param[in] status Status to print.
 */
void print_live_status(const dr_evt_grpc::JobStatus &status) {
  std::cout << "Job " << status.job_idx() << ": "
            << job_state_name(status.state()) << " (server";
  if (status.has_scheduled()) {
    std::cout << ", start=" << status.scheduled().start_time()
              << ", end=" << status.scheduled().end_time();
  } else if (status.has_expected_start_time()) {
    std::cout << ", expected_start=" << status.expected_start_time();
  }
  std::cout << ")\n";
}

#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
using RedisJob = std::map<std::string, std::string>;

/**
 * @brief Pipeline finalized-job lookups in the order supplied by the server.
 * @param[in] redis Redis connection used to create the pipeline.
 * @param[in] key_prefix Namespace containing the job hashes.
 * @param[in] job_ids IDs returned by AppendJobsRequest.
 * @return One hash per input ID in the same order; an empty hash means Redis
 * has not received that job in a finalized output flush.
 * @throws sw::redis::Error If Redis rejects or cannot execute the pipeline.
 */
std::vector<RedisJob>
get_finalized_jobs(sw::redis::Redis &redis, const std::string &key_prefix,
                   const google::protobuf::RepeatedField<uint32_t> &job_ids) {
  auto pipeline = redis.pipeline();
  for (const uint32_t job_id : job_ids) {
    pipeline.hgetall(key_prefix + ":job:" + std::to_string(job_id));
  }
  auto replies = pipeline.exec();

  std::vector<RedisJob> jobs(static_cast<std::size_t>(job_ids.size()));
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    replies.get(i, std::inserter(jobs[i], jobs[i].end()));
  }
  return jobs;
}

/**
 * @brief Print one finalized Redis job hash.
 * @param[in] job_id ID associated with @p job.
 * @param[in] job Finalized hash returned by Redis.
 */
void print_finalized_job(uint32_t job_id, const RedisJob &job) {
  const auto start = job.find("begin_time");
  const auto end = job.find("end_time");
  std::cout << "Job " << job_id << ": completed (Redis";
  if (start != job.end()) {
    std::cout << ", start=" << start->second;
  }
  if (end != job.end()) {
    std::cout << ", end=" << end->second;
  }
  std::cout << ")\n";
}
#endif
} // namespace

/**
 * @brief Synchronous request/response facade over a bidirectional gRPC stream.
 * @details A call is outstanding at most one at a time.  Monotonic request IDs
 * detect a mismatched response even though the transport preserves ordering.
 * The client owns the context, generated stub, and stream for their full RPC
 * lifetime; finish() must be called after the final request.
 */
class SimulationClient {
public:
  /** @brief Open a streaming session over an existing gRPC channel.
   * @param[in] channel Connected channel used to create the service stub. */
  explicit SimulationClient(std::shared_ptr<Channel> channel)
      : m_stub(SimulationService::NewStub(channel)),
        m_stream(m_stub->Session(&m_context)), m_next_request_id(1) {}

  /** @brief Send one request and wait for its matching response.
   * @param[in] req Client request; its request ID is replaced by this client.
   * @return Server response with the matching request ID.
   * @throws std::runtime_error for transport, ordering, or server errors.
   * @details Sends req (with a freshly assigned request_id) and blocks for
   * the matching response. Since this client issues one request at a time
   * and waits for its response before sending the next, request/response
   * ordering is trivially preserved. The request ID is still set and
   * checked as a sanity check, and to establish the pattern a more
   * pipelined client would need. */
  ServerMessage call(ClientMessage req) {
    uint64_t id = m_next_request_id++;
    req.set_request_id(id);

    if (!m_stream->Write(req)) {
      throw std::runtime_error(
          "Failed to write request to server (stream closed)");
    }

    ServerMessage resp;
    if (!m_stream->Read(&resp)) {
      throw std::runtime_error(
          "Failed to read response from server (stream closed)");
    }
    if (resp.request_id() != id) {
      throw std::runtime_error("Response request_id mismatch (got " +
                               std::to_string(resp.request_id()) +
                               ", expected " + std::to_string(id) + ")");
    }
    if (resp.response_case() == ServerMessage::kError) {
      throw std::runtime_error("Server error: " + resp.error().message());
    }
    return resp;
  }

  /** @brief Finish the streaming RPC and obtain its final status.
   * @return gRPC status returned by the server. */
  grpc::Status finish() {
    m_stream->WritesDone();
    return m_stream->Finish();
  }

private:
  /** @brief Generated service proxy used to create the session stream. */
  std::unique_ptr<SimulationService::Stub> m_stub;
  /** @brief Context owning cancellation, metadata, and status for this RPC. */
  ClientContext m_context;
  /** @brief Active bidirectional stream; valid from construction through
   * finish(). */
  std::unique_ptr<ClientReaderWriter<ClientMessage, ServerMessage>> m_stream;
  /** @brief Correlation ID assigned to the next request; zero is never emitted.
   */
  uint64_t m_next_request_id;
};

/** @brief Run the example gRPC streaming client.
 * @param[in] argc Command-line argument count.
 * @param[in] argv Command-line argument vector.
 * @return Process status: zero when the session completes successfully. */
int main(int argc, char **argv) {
  ClientOptions options;
  try {
    options = parse_options(argc, argv);
  } catch (const std::exception &e) {
    std::cerr << "Argument error: " << e.what() << "\n";
    print_usage(argv[0]);
    return 1;
  }

  // Parse each job's full data client-side. Unlike the old
  // submit_job()-only pattern (where the server independently loaded
  // the same file via InitializeTrace, and the client just echoed
  // back a submit_time the server already had), this file is *only*
  // ever read here - the server never sees it, and genuinely learns
  // about each job for the first time via AppendJobsRequest below.
  /** @brief Client-side representation of one row from the job CSV.
   * @details Values are forwarded to AppendJobsRequest without first loading
   * the trace on the server. */
  struct JobData {
    double submit_time; ///< Input arrival timestamp in simulation seconds.
    uint32_t num_nodes; ///< Input number of nodes requested by the job.
    std::string queue;  ///< Compile-time schema spelling of default Queue1.
    double limit_time;  ///< Input wall-time limit in simulation seconds.
  };
  std::vector<JobData> jobs;
  {
    std::ifstream ifs(options.job_data_file);
    if (!ifs) {
      std::cerr << "Failed to open job data file: " << options.job_data_file
                << std::endl;
      return 1;
    }
    std::string line;
    std::getline(ifs, line);
    if (line != "job_submit_time,num_nodes,time_limit") {
      std::cerr << "Expected queue-free simple job CSV header in "
                << options.job_data_file << std::endl;
      return 1;
    }
    while (std::getline(ifs, line)) {
      if (line.empty())
        continue;
      std::istringstream iss(line);
      std::string field;
      JobData j;
      std::getline(iss, field, ',');
      j.submit_time = std::stod(field);
      std::getline(iss, field, ',');
      j.num_nodes = static_cast<uint32_t>(std::stoul(field));
      std::getline(iss, field, ',');
      j.queue = kDefaultQueueInput;
      j.limit_time = std::stod(field);
      jobs.push_back(j);
    }
  }

  SimulationClient client(
      grpc::CreateChannel(options.server_address,
                          grpc::InsecureChannelCredentials()));

  try {
    // 1. Initialize the simulation on the server. infile is only
    // used for its header (Trace's constructor validates the
    // column format) - its data rows are never read, since step 2
    // below (InitializeTraceRequest) is deliberately never sent.
    ClientMessage init_req;
    auto *init = init_req.mutable_init();
    init->set_total_nodes(100);
    init->set_trace_format("simple");
    init->set_timestamp_format("epoch");
    init->set_backfill_policy("easy");
    init->set_priority_policy("fcfs");
    init->set_run_time_mode(
        "limit"); // appended jobs have no actual_run_time column to read
    init->set_infile(options.job_data_file);
    init->set_queue_impl("circular");
    init->set_session_name("example-client");
    if (!options.redis_uri.empty()) {
      init->set_redis_uri(options.redis_uri);
      init->set_redis_key_prefix(options.redis_key_prefix);
      init->set_job_flush_interval(1);
    }
    client.call(init_req);
    std::cout << "Session initialized. Server has not loaded any jobs yet.\n";

    // 2. Append every job in one batch. The returned IDs preserve request
    // order and are used for the Redis/status query below.
    ClientMessage append_req;
    auto *append = append_req.mutable_append_jobs();
    for (const auto &j : jobs) {
      auto *a = append->add_requests();
      a->set_submit_time(j.submit_time);
      a->set_num_nodes(j.num_nodes);
      a->set_queue(j.queue);
      a->set_limit_time(j.limit_time);
    }
    const auto append_resp = client.call(append_req).append_jobs();
    std::cout << "Appended " + std::to_string(jobs.size()) +
                     " jobs the server had never seen before.\n";

    if (options.query_after_advance) {
      ClientMessage advance_req;
      advance_req.mutable_advance_to()->set_target_time(options.advance_to);
      client.call(advance_req);
      std::cout << "Advanced simulation to " << options.advance_to << ".\n";

      ClientMessage status_req;
      auto *status_ids = status_req.mutable_get_job_statuses();

#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
      std::vector<RedisJob> finalized_jobs;
      if (!options.redis_uri.empty()) {
        sw::redis::Redis redis(options.redis_uri);
        finalized_jobs = get_finalized_jobs(
            redis, options.redis_key_prefix, append_resp.job_idx());
        for (int i = 0; i < append_resp.job_idx_size(); ++i) {
          if (finalized_jobs[static_cast<std::size_t>(i)].empty()) {
            status_ids->add_job_idx(append_resp.job_idx(i));
          }
        }
      } else
#endif
      {
        for (const uint32_t job_id : append_resp.job_idx()) {
          status_ids->add_job_idx(job_id);
        }
      }

      dr_evt_grpc::GetJobStatusesResponse live_statuses;
      if (status_ids->job_idx_size() != 0) {
        live_statuses = client.call(status_req).get_job_statuses();
      }

      std::cout << "\n=== Job Statuses ===\n";
      int live_index = 0;
      for (int i = 0; i < append_resp.job_idx_size(); ++i) {
#if defined(DR_EVT_HAS_REDIS_PLUS_PLUS)
        if (!options.redis_uri.empty() &&
            !finalized_jobs[static_cast<std::size_t>(i)].empty()) {
          print_finalized_job(append_resp.job_idx(i),
                              finalized_jobs[static_cast<std::size_t>(i)]);
          continue;
        }
#endif
        print_live_status(live_statuses.jobs(live_index++));
      }
    }

    // 3. Finish declares there will be no more arrivals. It drains all
    // submitted work, writes session-scoped reports, and resets only
    // this stream's simulation; dr_evt_server itself keeps running.
    ClientMessage finish_req;
    finish_req.mutable_finish_simulation();
    auto finish_resp = client.call(finish_req).finish_simulation();
    const auto &stats = finish_resp.statistics();

    std::cout << "\n=== Final Statistics ===\n"
              << "Jobs submitted:  " << stats.jobs_submitted() << "\n"
              << "Jobs started:    " << stats.jobs_started() << "\n"
              << "Jobs completed:  " << stats.jobs_completed() << "\n"
              << "Current time:    " << stats.current_time() << "\n"
              << "Resource area:   " << stats.resource_area()
              << " node-seconds\n"
              << "Utilization:     " << (stats.utilization() * 100.0) << "%\n"
              << "Avg wait time:   " << stats.avg_wait_time() << "\n"
              << "Makespan:        " << stats.makespan() << "\n"
              << "Session ID:      " << finish_resp.session_id() << "\n"
              << "Statistics file: " << finish_resp.statistics_file() << "\n";

    // To reuse this same bidirectional stream, send another InitRequest
    // here and run its append/submit/finish sequence; call client.finish()
    // only when no further simulations will use the stream.
  } catch (const std::exception &e) {
    std::cerr << "Client error: " << e.what() << std::endl;
    client.finish();
    return 1;
  }

  grpc::Status status = client.finish();
  if (!status.ok()) {
    std::cerr << "RPC failed: " << status.error_message() << std::endl;
    return 1;
  }
  return 0;
}
