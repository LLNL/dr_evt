/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_JOB_RECORD_HPP
#define DR_EVT_TRACE_JOB_RECORD_HPP

#include <vector>
#include <string>
#include "common.hpp"
#include "trace/epoch.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

class Job_Record {
  protected:
    epoch_t m_t_begin; ///< The starting time of the job execution
    epoch_t m_t_end; ///< The end time of job execution
    epoch_t m_t_submit; ///< The time when the job was submitted
    timeout_t m_t_limit; ///< The time limit of the job
    num_nodes_t m_num_nodes; ///< The amount of resources this job uses
    job_queue_t m_q; ///< The queue to which the job was submitted
  #if SHOW_ORG_NO
    job_no_t m_org_no;
  #endif
  #if MARK_DAT_PERIOD
    /** Is the job duration overlaps with DAT period?
     *  i.e., the period of other job from pAll queue
     */
    bool m_dat;
  #endif

    /// Number of nodes being used at the submit time
    num_nodes_t m_busy_nodes;

    /// Number of inputs which the constructor is expecting
    static unsigned int num_inputs;


  public:
  #if SHOW_ORG_NO
    Job_Record(job_no_t n, const std::vector<std::string>& svec) noexcept(false);
  #else
    Job_Record(const std::vector<std::string>& str_vec) noexcept(false);
  #endif

    Job_Record(const Job_Record& other);
    Job_Record(Job_Record&& other) noexcept;
    Job_Record& operator=(const Job_Record& rhs);
    Job_Record& operator=(Job_Record&& rhs) noexcept;

    static void set_num_inputs(num_nodes_t n) { num_inputs = n; }
    epoch_t get_begin_time() const { return m_t_begin; }
    epoch_t get_end_time() const { return m_t_end; }
    epoch_t get_submit_time() const { return m_t_submit; }
    tdiff_t get_exec_time() const { return (m_t_end - m_t_begin); }
    tdiff_t get_wait_time() const { return (m_t_begin - m_t_submit); }
    timeout_t get_limit_time() const { return m_t_limit; }
    num_nodes_t get_num_nodes() const { return m_num_nodes; }
    num_nodes_t get_busy_nodes() const { return m_busy_nodes; }
  #if MARK_DAT_PERIOD
    bool does_overlap_dat() const { return m_dat; }
    void set_busy_nodes(num_nodes_t n, bool dat = false)
    { m_busy_nodes = (!dat)*n + dat*total_nodes; m_dat = dat; }
    void set_dat() { m_dat = true; }
  #else
    void set_busy_nodes(num_nodes_t n) { m_busy_nodes = n; }
  #endif
    job_queue_t get_queue() const { return m_q; }

  #if SHOW_ORG_NO
    void set_org_line_no(job_no_t i) { m_org_no = i; }
  #endif

    std::string to_string() const;
    static std::string get_header_str();

    friend bool operator<(const Job_Record& r1, const Job_Record& r2);
};

inline bool operator<(const Job_Record& r1, const Job_Record& r2)
{
    return(r1.m_t_submit < r2.m_t_submit);
}

std::ostream& operator<<(std::ostream& os, const Job_Record& rec);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_RECORD_HPP
