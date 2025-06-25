/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_JOB_STAT_TIME
#define DR_EVT_TRACE_JOB_STAT_TIME
#include <map>
#include <array>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include "common.hpp"
#include "trace/trace.hpp"
#include "sim/job_time_common.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 *  Statistics on job run time given timeout limit.
 */
class Job_Stat_Time {
  public:
    using ratio_t = float;
    using bin_info_t = std::pair<ratio_t, num_jobst_t>;
    /**
     *  Execution time distribution. If the number of bins is 4 for instance,
     *  each bin represents a quartile of the timeout--[0%, 25%), [25%, 50%),
     *  [50%, 75%), and [75%, 100%)--to which the execution time of each job
     *  falls into. The value of a bin represents the number of jobs belong to
     *  the quartile, and the distribution shows how long jobs actually ran
     *  compared to the timeout set by users.
     */
    using tbins_t = typename std::vector<bin_info_t>;

    /**
     *  Execution time distribution for each resource set, i.e., the number of
     *  nodes.
     */
    using tbins_by_nnodes_t = typename std::map<num_nodes_t, tbins_t>;

    struct timeout_slot {
        num_jobs_t m_num_jobs;
        tbins_by_nnodes_t m_bins;

        timeout_slot() : m_num_jobs(static_cast<num_jobs_t>(0u)) {};
    };

    using tjob_t = std::map<timeout_t, timeout_slot>;

  protected:
    size_t m_default_num_bins;
    tjob_t m_tjob;
    void add_stat(const Job_Record& j);

  public:
    Job_Stat_Time(const size_t N=4);

    void process(const Trace& trace);
};

/**
 *  Change the bins of job counts to the list of accumulated quantity and
 *  probability.
 */
void Job_Stat_Time::finalize()
{
    for (auto& slot_tout: m_tjobs) {
        for (auto& slot_nodes: slot_tout.m_bins) {
             auto sum = static_cast<num_jobs_t>(0u);
             for (auto& slot_texe: slot_nodes) {
                 auto cnt = slot_texe.second;
                 slot_texe.second = sum;
                 sum += cnt;
             }

             if (sum == static_cast<num_jobs_t>(0u)) {
                 // A slot is created only when there is a job to add
                 std::string err = "Total number of jobs cannot be zero.";
                 throw std::runtme_error(err);
             }
             const auto sum_f = static_cast<ratio_t>(sum);
             for (auto& slot_texe: slot_nodes) {
                 slot_texe.first = slot_texe.second/sum_f;
             }
        }
    }
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_STAT_TIME
