/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_JOB_STAT_TEXEC
#define DR_EVT_TRACE_JOB_STAT_TEXEC
#include <map>
#include <array>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include "common.hpp"
#include "trace/job_record.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 *  Statistics on job run time given timeout limit.
 */
template <size_t N>
class Job_Stat_Texec {
  public:
    /**
     *  Execution time distribution. If the number of bins is 4 for instance,
     *  each bin represents a quartile of the timeout--[0%, 25%), [25%, 50%),
     *  [50%, 75%), and [75%, 100%)--to which the execution time of each job
     *  falls into. The value of a bin represents the number of jobs belong to
     *  the quartile, and the distribution shows how long jobs actually ran
     *  compared to the timeout set by users.
     */
    using tbins_t = typename std::array<num_jobs_t, N>;

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
    tjob_t m_tjob;

  public:
    Job_Stat_Texec();

    void add_stat(const Job_Record& j);
};

template <std::size_t N>
Job_Stat_Texec<N>::Job_Stat_Texec()
{
    if (N == 0ul) {
        std::string err = "Number of limit time segments should not be zero.";
        throw std::invalid_argument(err);
    }
}

template <std::size_t N>
void Job_Stat_Texec<N>::add_stat(const Job_Records& j)
{
    const auto t_exec = j.get_exec_time();
    const auto t_limit = j.get_limit_time()
    const auto n_nodes = j.get_num_nodes();

    auto i = std::max(static_cast<size_t>((t_exec / t_limit) * N), N-1);
    const slot& = m_tjobs[t_limit];
    slot.m_bins[n_nodes][i] ++; // This does not work without initialization
    slot.m_num_jobs ++;
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_STAT_TEXEC
