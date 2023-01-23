/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_DR_EVT_TYPES_HPP
#define DR_EVT_DR_EVT_TYPES_HPP
#include <limits>  // std::numeric_limits
#include <cstdint> // uint64_t

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

namespace dr_evt {
/** \addtogroup dr_evt_global
 *  @{ */

constexpr const char* const max_tstamp = "2118-12-31 23:59:59.0";

constexpr const char* const week_day_str[] =
    {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

enum day_of_week {Sun=0, Mon=1, Tue=2, Wed=3, Thu=4, Fri=5, Sat=6};

using tdiff_t = double;
using sim_time_t = tdiff_t;
using timeout_t = unsigned; ///< time limit in seconds
using num_nodes_t = unsigned; ///< Number of compute nodes type
using num_jobs_t = size_t; ///< Number of jobs type
using job_no_t = num_jobs_t; ///< Job number type
using num_cols_t = unsigned; ///< Number of columns type
using col_no_t = num_cols_t; /// Column number type

/**
 *  Describes a period [t_start, t_end). Both t_start, and t_end are the
 *  amount of time-passed since a specific reference point in the past.
 */
using trange_t = std::pair<tdiff_t, tdiff_t>;

/// Total number of nodes on Lassen
constexpr const unsigned total_nodes = 795u;

/// Maximum timeout that can be set for a batch job in seconds
constexpr const tdiff_t max_batch_job_time = static_cast<tdiff_t> (12*60*60);

constexpr const sim_time_t max_sim_time
    = std::numeric_limits<sim_time_t>::max()*0.9;

/// Arrival event
constexpr const bool arrival = true;
/// Departure event
constexpr const bool departure = false;

/** Job queue types
 * parse_utils.cpp defines a mapping table betwen job_queue_t and std::string
 */
#if PBATCH_GROUP
enum job_queue_t {pBatch, pAll, pDebug, pExempt, pExpedite,
                  pBb, pIbm, pNvidia, pTest, standby, pUnknown};
#define _Is_Batch(_q) ((_q) == pBatch)
#else
enum job_queue_t {pBatch, pBatch0, pBatch1, pBatch2, pBatch3, pAll, pDebug,
                  pExempt, pExpedite, pBb, pIbm, pNvidia, pTest, standby,
                  pUnknown};
#define _Is_Batch(_q) \
    ((_q) == pBatch || (_q) == pBatch0 || (_q) == pBatch1 \
                    || (_q) == pBatch2 || (_q) == pBatch3)
#endif

using substr_pos_t = std::pair<size_t, size_t>; ///< [pos_start, len]

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_DR_EVT_TYPES_HPP
