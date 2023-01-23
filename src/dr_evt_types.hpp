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
#include <iostream>
#include <utility> // std::move
#include <memory>  // std::unique_ptr
#include <limits>  // std::numeric_limits
#include <cstdint> // uint64_t

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if defined(DR_EVT_HAS_METIS)
#include <metis.h>
#endif // defined(DR_EVT_HAS_METIS)

namespace dr_evt {
/** \addtogroup dr_evt_global
 *  @{ */

using sim_time_t = double;
using num_jobs_t = unsigned; // or size_t
using tstamp_t = std::string; ///< Timestamp format: YYYY-MM-DD HH:MM:SS.ssssss

constexpr const sim_time_t max_sim_time
    = std::numeric_limits<sim_time_t>::max()*0.9;

constexpr const char* const max_tstamp = "2100-01-01 23-01-01.000001";

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_DR_EVT_TYPES_HPP
