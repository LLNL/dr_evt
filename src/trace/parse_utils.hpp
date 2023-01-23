/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_PARSE_UTILS_HPP
#define DR_EVT_TRACE_PARSE_UTILS_HPP

#include <string>
#include <vector>
#include "common.hpp"
#include "trace/epoch.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

void set_by(epoch_t& t, const std::string& str);
void set_by(unsigned& v, const std::string& str);
void set_by(double& v, const std::string& str);
void set_by(job_queue_t& q, const std::string& str);
std::string to_string(const job_queue_t q);

/**
 * Removes leading and trailing spaces from a string
 */
std::string trim(const std::string& str,
                  const std::string& whitespace = " \t");

/**
 *  Given a csv string, return a vector of the position of each value,
 *  [start, end), in the stringa.
 */
std::vector<substr_pos_t> comma_separate(const std::string& str);

/**
 *  This is a data-specific helper routine.
 *  Replace the comma, which is a delimiter, within a (double) quotation.
 *  Without this, parsing comma-sepated-value data may result in an error.
 *  Does not handle a case as "'...,..."' where quotation is done erroneously.
 */
void replace_comma_within_quotation(std::string& line);


/**
 *  Case-insensitive substring search
 */
bool search_ci(const std::string& str, const std::string& sub);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_PARSE_UTILS_HPP
