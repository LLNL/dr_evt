#ifndef DR_EVT_SIM_JOB_SUBMIT_COMMON_HPP
#define DR_EVT_SIM_JOB_SUBMIT_COMMON_HPP
#include <array>
#include <vector>
#include "common.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_sim
 *  @{ */

/// Number of submissions in an hour-slot observed over N weeks
using submit_hour_t = std::vector<num_jobs_t>;

/// Number of submissions in each hour-slot in a week observed over N weeks
using submit_week_t = typename std::array<submit_hour_t, 7*24>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_JOB_SUBMIT_COMMON_HPP
