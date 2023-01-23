/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_EPOCH_HPP
#define DR_EVT_TRACE_EPOCH_HPP

#include <ctime>
#include <iostream>
#include <string>
#include <array>
#include "common.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * <integral seconds, fractional second>
 */
using epoch_t = std::pair<time_t, float>;
using period_t = std::pair<epoch_t, epoch_t>;

/// range from 0 to 7*24-1 as day of week [0-7) and hour [0-24)
using hour_bin_id_t = unsigned;


inline bool operator<(const epoch_t& t1, const epoch_t& t2)
{
    return ((t1.first < t2.first) ||
            ((t1.first == t2.first) &&
             (t1.second < t2.second)));
}

inline bool operator==(const epoch_t& t1, const epoch_t& t2)
{
    return ((t1.first == t2.first) && (t1.second == t2.second));
}

inline bool operator< (const period_t& t1, const period_t& t2)
{
    return ((t1.first < t2.first) ||
            ((t1.first == t2.first) &&
             (t1.second < t2.second)));
}

inline tdiff_t operator-(const epoch_t& t1, const epoch_t& t2)
{
    return std::difftime(t1.first, t2.first) + (t1.second - t2.second);
}

std::string to_string(const epoch_t& t);

std::ostream& operator<<(std::ostream& os, const epoch_t& t);

/**
 *  Check if the give string is timestamp
 */
bool is_timestamp(const std::string& time_str);

/**
 *  Return seconds (epoch) converted from the time string given as well as the
 *  fractional second.
 */
epoch_t convert_time(const std::string& time_str);

/**
 *  Return the hour index of a given time, which is the number of hours passed
 *  since the beginning of the week.
 */
hour_bin_id_t get_hour_bin_id(const std::time_t t);

/// Return the day of week of a given time
day_of_week weekday(const epoch_t& e);

/// Return the beginning time of next week based on a given time
std::time_t get_time_of_next_week_start(const std::time_t t);
std::time_t get_time_of_next_week_start(const epoch_t& t);

/// Return the beginning time of current week based on a given time
std::time_t get_time_of_cur_week_start(const std::time_t t);
std::time_t get_time_of_cur_week_start(const epoch_t& t);

void hour_boundaries_of_week(const std::time_t t,
                             std::array<std::time_t, 7*24+1>& bo);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_EPOCH_HPP
