/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "trace/job_stat_time.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

Job_Stat_Time::Job_Stat_Time(const size_t N)
  : m_default_num_bins(N)
{
    if (N == 0ul) { // Can be constexpr if C++17 and newer
        std::string err = "Number of bins for timeouts should not be zero.";
        throw std::invalid_argument(err);
    }
}

void Job_Stat_Time::add_stat(const Job_Records& j)
{
    const auto t_exec = j.get_exec_time();
    const auto t_limit = j.get_limit_time()
    const auto n_nodes = j.get_num_nodes();

    auto slot& = m_tjobs[t_limit];
    auto slot_nodes& = slot.m_bins[n_nodes];
  
    if (slot_nodes.empty()) {
        // TODO: choose different number of bins based on the expected number of
        // samples.
        // For a smaller number of samples, the number of bins can be smaller.
        slot_nodes.resize(m_default_num_bins + 1u,
                          std::make_pair(static_cast<ratio_t>(0.0),
                                         static_cast<num_jobs_t>(0u)));
    }
    const auto N = slot_nodes.size() - 1u;
    // Assume that t_exec is in (0, t_limit] and falls in to the bin
    // (t_limit/N*i t_limit/N*(i+1)]
    const auto i = std::min(static_cast<size_t>((t_exec / t_limit) * N), N-1);
    slot_nodes[i].second ++;
}

/**@}*/
} // end of namespace dr_evt
