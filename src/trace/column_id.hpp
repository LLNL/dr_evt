/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_COLUMN_ID_HPP
#define DR_EVT_TRACE_COLUMN_ID_HPP

#include <string>
#include "common.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 * <column index, column title>
 * Colum index starts from 0.
 * column title can be found in the first line of the data file.
 */
using column_id_t = std::pair<col_no_t, std::string>;

inline bool operator<(const column_id_t& c1, const column_id_t& c2)
{ // No need for tie-breaking for this problem
    return (c1.first < c2.first);
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_COLUMN_ID_HPP
