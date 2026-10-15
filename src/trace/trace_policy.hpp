/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file trace_policy.hpp
 * @brief Compile-time storage policies: standard, Pcon, and SWF traces. Each
 * policy names its record type and loader; add a new trace type as a new
 * policy here, a TraceType value, and a dispatch case in src/sim.cpp.
 */

#ifndef DR_EVT_TRACE_TRACE_POLICY_HPP
#define DR_EVT_TRACE_TRACE_POLICY_HPP

#include "trace/job_record.hpp"
#include <boost/circular_buffer.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace dr_evt {

class Data_Columns;

/** Values carried by one job in the Pcon experiment. */
struct Pcon_Values {
  double avgpcon = 0.0;
  double minpcon = 0.0;
  double maxpcon = 0.0;
};

/**
 * Pcon record stored by value. Inheritance lets the scheduling core
 * continue to consume Job_Record references while the concrete buffer retains
 * the additional fields. No pointer or dynamically-sized payload is added to
 * an individual record.
 */
class Pcon_Job_Record : public Job_Record {
public:
  using Job_Record::Job_Record;

  Pcon_Job_Record(Job_Record &&job, const Pcon_Values &pcon = {})
      : Job_Record(std::move(job)), m_pcon(pcon) {}

  const Pcon_Values &pcon() const { return m_pcon; }

private:
  Pcon_Values m_pcon;
};

/** Standard resource-history entry; preserves the existing representation. */
struct Standard_Resource_Sample {
  epoch_t time;
  num_nodes_t allocated;
  num_nodes_t capacity;
};

/** Resource-history entry emitted by the Pcon experiment. */
struct Pcon_Resource_Sample {
  epoch_t time;
  num_nodes_t allocated;
  num_nodes_t capacity;
  Pcon_Values pcon;
};

struct Standard_Trace_Policy {
  using record_type = Job_Record;
  using job_store_type = boost::circular_buffer<record_type>;
  using resource_sample_type = Standard_Resource_Sample;

  static int load_records(const std::string &fname, const Data_Columns &dcols,
                          std::vector<record_type> &records,
                          num_jobs_t max_count);
  static record_type make_record(const epoch_t &submit_time,
                                 num_nodes_t num_nodes, job_queue_t queue,
                                 timeout_t limit_time) {
    return record_type(submit_time, num_nodes, queue, limit_time);
  }

  static resource_sample_type sample(const epoch_t &time, num_nodes_t allocated,
                                     num_nodes_t capacity) {
    return {time, allocated, capacity};
  }
  static const char *resource_columns() { return ""; }
  static std::string resource_values(const resource_sample_type &) {
    return "";
  }
  /** @brief Reset runtime accounting before restoring running jobs. */
  void reset_runtime_state() {}
  void on_start(const record_type &) {}
  void on_finish(const record_type &) {}
};

struct Pcon_Trace_Policy {
  using record_type = Pcon_Job_Record;
  using job_store_type = boost::circular_buffer<record_type>;
  using resource_sample_type = Pcon_Resource_Sample;

  static int load_records(const std::string &fname, const Data_Columns &dcols,
                          std::vector<record_type> &records,
                          num_jobs_t max_count);
  static record_type make_record(const epoch_t &submit_time,
                                 num_nodes_t num_nodes, job_queue_t queue,
                                 timeout_t limit_time) {
    return record_type(Job_Record(submit_time, num_nodes, queue, limit_time));
  }

  resource_sample_type sample(const epoch_t &time, num_nodes_t allocated,
                              num_nodes_t capacity) const {
    return {time, allocated, capacity, m_current};
  }
  static const char *resource_columns() { return ",avgpcon,minpcon,maxpcon"; }
  static std::string resource_values(const resource_sample_type &sample) {
    return "," + std::to_string(sample.pcon.avgpcon) + "," +
           std::to_string(sample.pcon.minpcon) + "," +
           std::to_string(sample.pcon.maxpcon);
  }
  /** @brief Reset accumulated Pcon values before restoring running jobs. */
  void reset_runtime_state() { m_current = {}; }
  void on_start(const record_type &job) {
    const auto &pcon = job.pcon();
    m_current.avgpcon += pcon.avgpcon;
    m_current.minpcon += pcon.minpcon;
    m_current.maxpcon += pcon.maxpcon;
  }
  void on_finish(const record_type &job) {
    const auto &pcon = job.pcon();
    m_current.avgpcon -= pcon.avgpcon;
    m_current.minpcon -= pcon.minpcon;
    m_current.maxpcon -= pcon.maxpcon;
  }

private:
  Pcon_Values m_current;
};

/**
 * Record loaded from a Standard Workload Format (SWF) file. The scheduling
 * core consumes it as a Job_Record; the SWF-specific identity fields are kept
 * alongside by value, so no pointer or dynamic payload is added per record.
 * The base queue is always Queue1 (the simulator's queue ids are semantic);
 * the file's own queue number is preserved here for analysis.
 */
class Swf_Job_Record : public Job_Record {
public:
  using Job_Record::Job_Record;

  /**
   * @brief Wrap a parsed job record with its SWF identity fields.
   * @param[in] job Record built from the SWF fields the simulator uses.
   * @param[in] swf_job_number SWF field 1, the job number in the file.
   * @param[in] swf_queue SWF field 15; 0 when missing (-1) or above 255.
   */
  Swf_Job_Record(Job_Record &&job, std::uint32_t swf_job_number,
                 std::uint8_t swf_queue)
      : Job_Record(std::move(job)), m_swf_job_number(swf_job_number),
        m_swf_queue(swf_queue) {}

  /** @brief Report the job number from the SWF file.
   *  @return SWF field 1, or 0 when it was missing or out of range. */
  std::uint32_t swf_job_number() const { return m_swf_job_number; }
  /** @brief Report the queue number from the SWF file.
   *  @return SWF field 15, or 0 when missing (-1) or above 255. */
  std::uint8_t swf_queue() const { return m_swf_queue; }

private:
  std::uint32_t m_swf_job_number = 0u;
  std::uint8_t m_swf_queue = 0u;
};

/**
 * Policy for Standard Workload Format input. Identical to the standard policy
 * except that load_records() parses SWF directly: ';' header lines
 * (UnixStartTime, MaxNodes, MaxProcs), 18 whitespace-separated fields, -1 for
 * missing values, processors converted to nodes with a cores-per-node divisor.
 */
struct Swf_Trace_Policy {
  using record_type = Swf_Job_Record;

  /** @brief Read MaxNodes from an SWF file's ';' header without loading it.
   *  @param[in] fname Path of the SWF file.
   *  @return The value, or 0 when the file or the key is absent. */
  static unsigned header_max_nodes(const std::string &fname);

  using job_store_type = boost::circular_buffer<record_type>;
  using resource_sample_type = Standard_Resource_Sample;

  /**
   * @brief Parse an SWF file into job records.
   * @param[in] fname Path of the SWF file.
   * @param[in] dcols Column configuration; supplies the cores-per-node
   *        override and receives the load diagnostics.
   * @param[out] records Destination, appended to and sorted by submit time
   *        over the records this call added.
   * @param[in] max_count Maximum records to accept; 0 means no limit.
   * @return EXIT_SUCCESS, or EXIT_FAILURE when the file cannot be opened.
   * @throws std::invalid_argument on a malformed line or an out-of-range
   *         node count or time limit.
   */
  static int load_records(const std::string &fname, const Data_Columns &dcols,
                          std::vector<record_type> &records,
                          num_jobs_t max_count);
  /**
   * @brief Build a record for a job appended at run time.
   * @param[in] submit_time Arrival time.
   * @param[in] num_nodes Nodes requested.
   * @param[in] queue Queue identity.
   * @param[in] limit_time Time limit in seconds.
   * @return Record with no SWF identity (job number and queue 0).
   */
  static record_type make_record(const epoch_t &submit_time,
                                 num_nodes_t num_nodes, job_queue_t queue,
                                 timeout_t limit_time) {
    return record_type(Job_Record(submit_time, num_nodes, queue, limit_time),
                       0u, 0u);
  }

  /** @brief Build a resource-history sample.
   *  @param[in] time Sample time.
   *  @param[in] allocated Nodes in use.
   *  @param[in] capacity Nodes available in total.
   *  @return Standard sample; SWF adds no resource columns. */
  static resource_sample_type sample(const epoch_t &time, num_nodes_t allocated,
                                     num_nodes_t capacity) {
    return {time, allocated, capacity};
  }
  /** @brief Extra resource-trace header columns. @return Empty string. */
  static const char *resource_columns() { return ""; }
  /** @brief Extra resource-trace values for a sample. @return Empty string. */
  static std::string resource_values(const resource_sample_type &) {
    return "";
  }
  /// @brief Reset runtime accounting before restoring running jobs (no-op).
  void reset_runtime_state() {}
  /// @brief Hook called when a job starts (no-op for SWF).
  void on_start(const record_type &) {}
  /// @brief Hook called when a job finishes (no-op for SWF).
  void on_finish(const record_type &) {}
};

} // namespace dr_evt

#endif // DR_EVT_TRACE_TRACE_POLICY_HPP
