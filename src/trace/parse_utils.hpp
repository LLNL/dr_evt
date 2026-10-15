/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file parse_utils.hpp
 * @brief Trace-field parsing, CSV preprocessing, and string helpers.
 */

#ifndef DR_EVT_TRACE_PARSE_UTILS_HPP
#define DR_EVT_TRACE_PARSE_UTILS_HPP

#include "common.hpp"
#include "trace/epoch.hpp"
#include <string>
#include <vector>

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/** Timestamp encoding selected once for an input trace or schedule. */
enum class TimestampEncoding { EPOCH, CALENDAR };

/** @brief Detect whether one representative value is numeric epoch time or a
 * calendar timestamp. */
TimestampEncoding detect_timestamp_encoding(const std::string &str);

/** @brief Parse an epoch timestamp. @param[out] t Parsed timestamp. @param[in]
 * str Input text. */
void set_by(epoch_t &t, const std::string &str);
/** @brief Parse a timestamp using an encoding already selected for its input.
 * @param[out] t Parsed timestamp.
 * @param[in] str Input text.
 * @param[in] encoding Encoding detected once for the containing input. */
void set_by(epoch_t &t, const std::string &str, TimestampEncoding encoding);
/** @brief Parse a non-negative integer, rejecting a sign, trailing text, and
 * values above @p max.
 * @param[in] str Input text.
 * @param[in] max Largest accepted value, normally the destination type's max.
 * @return The parsed value.
 * @throws std::invalid_argument on any malformed or out-of-range input. */
unsigned long long parse_unsigned(const std::string &str,
                                  unsigned long long max);
/** @brief Parse an unsigned integer. @param[out] v Parsed value. @param[in] str
 * Input text. */
void set_by(unsigned &v, const std::string &str);
/** @brief Parse a floating-point value. @param[out] v Parsed value. @param[in]
 * str Input text. */
void set_by(double &v, const std::string &str);
/** @brief Parse a queue name. @param[out] q Parsed queue enum. @param[in] str
 * Input text. */
void set_by(job_queue_t &q, const std::string &str);
/** @brief Parse a one-based, site-neutral queue ID.
 * @param[out] q Parsed queue enum.
 * @param[in] str Numeric queue identifier. */
void set_by_queue_id(job_queue_t &q, const std::string &str);
#if DR_EVT_LEGACY_QUEUE_INPUT
/** @brief Format a queue enum as its canonical legacy name.
 * @param[in] q Queue value. @return Canonical queue name. */
std::string to_string(const job_queue_t q);
#endif

/**
 * @brief Remove leading and trailing delimiter characters.
 * @param[in] str Source string.
 * @param[in] whitespace Characters considered removable whitespace.
 * @return Trimmed string value.
 */
std::string trim(const std::string &str, const std::string &whitespace);

/** @brief Remove leading and trailing ASCII whitespace.
 * @param[in] str Source string.
 * @return Trimmed string value. */
inline std::string trim(const std::string &str) { return trim(str, " \t"); }

/**
 * @brief Locate CSV field ranges in one line.
 * @param[in] str CSV input line.
 * @return Half-open [start, end) ranges for each field.
 */
std::vector<substr_pos_t> comma_separate(const std::string &str);

/**
 * @brief Split a line on runs of spaces and tabs (SWF and similar formats).
 * @param[in] str Whitespace-separated input line.
 * @return Half-open [start, end) ranges for each non-empty token.
 */
std::vector<substr_pos_t> whitespace_separate(const std::string &str);

/**
 * @brief Replace delimiter commas occurring inside quoted CSV fields.
 *
 *  This is a data-specific helper routine.
 *  Replace the comma, which is a delimiter, within a (double) quotation.
 *  Without this, parsing comma-sepated-value data may result in an error.
 *  Does not handle a case as "'...,..."' where quotation is done erroneously.
 * @param[in,out] line CSV line modified in place.
 */
void replace_comma_within_quotation(std::string &line);

/**
 * @brief Perform a case-insensitive substring search.
 * @param[in] str String to search.
 * @param[in] sub Candidate substring.
 * @return true when sub occurs in str without regard to case.
 */
bool search_ci(const std::string &str, const std::string &sub);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_PARSE_UTILS_HPP
