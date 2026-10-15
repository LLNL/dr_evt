/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file parse_utils.cpp
 * @brief Trace-field parsing and CSV preprocessing implementation.
 */

#include "trace/parse_utils.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace dr_evt {

/** @brief Mapping from legacy queue names to scheduler queue identities. */
std::unordered_map<std::string, job_queue_t> str2jobq{
#if PBATCH_GROUP
    {"pbatch", Queue1},   {"pbatch0", Queue1}, {"pbatch1", Queue1},
    {"pbatch2", Queue1},  {"pbatch3", Queue1},
#else
    {"pbatch", Queue1},   {"pbatch0", Queue2}, {"pbatch1", Queue3},
    {"pbatch2", Queue4},  {"pbatch3", Queue5},
#endif
#if PBATCH_GROUP
    {"pall", Queue2},     {"pdebug", Queue3},  {"exempt", Queue4},
    {"expedite", Queue5}, {"pbb", Queue6},     {"pibm", Queue7},
    {"pnvidia", Queue8},  {"ptest", Queue9},   {"standby", Queue10},
#else
    {"pall", Queue6},     {"pdebug", Queue7},  {"exempt", Queue8},
    {"expedite", Queue9}, {"pbb", Queue10},    {"pibm", Queue11},
    {"pnvidia", Queue12}, {"ptest", Queue13},  {"standby", Queue14},
#endif
    {"", QueueUnknown}};

#if DR_EVT_LEGACY_QUEUE_INPUT
std::map<job_queue_t, std::string> jobq2str{
#if PBATCH_GROUP
    {Queue1, "pbatch"},
#else
    {Queue1, "pbatch"},    {Queue2, "pbatch0"}, {Queue3, "pbatch1"},
    {Queue4, "pbatch2"},   {Queue5, "pbatch3"},
#endif
#if PBATCH_GROUP
    {Queue2, "pall"},      {Queue3, "pdebug"}, {Queue4, "pexempt"},
    {Queue5, "pexpedite"}, {Queue6, "pbb"},    {Queue7, "pibm"},
    {Queue8, "pnvidia"},   {Queue9, "ptest"},  {Queue10, "standby"},
#else
    {Queue6, "pall"},      {Queue7, "pdebug"},  {Queue8, "pexempt"},
    {Queue9, "pexpedite"}, {Queue10, "pbb"},    {Queue11, "pibm"},
    {Queue12, "pnvidia"},  {Queue13, "ptest"},  {Queue14, "standby"},
#endif
    {QueueUnknown, ""}};
#endif

TimestampEncoding detect_timestamp_encoding(const std::string &str) {
  size_t pos = 0;
  try {
    std::stod(str, &pos);
    if (pos == str.size()) {
      return TimestampEncoding::EPOCH;
    }
  } catch (const std::exception &) {
  }
  return TimestampEncoding::CALENDAR;
}

void set_by(epoch_t &t, const std::string &str, TimestampEncoding encoding) {
  if (str.empty()) {
    t = {0, 0.0f};
    return;
  }

  if (encoding == TimestampEncoding::EPOCH) {
    // Parse as Unix epoch seconds
    size_t pos = 0;
    const double seconds = std::stod(str, &pos);
    if (pos != str.size()) {
      throw std::invalid_argument{"Failed to parse epoch timestamp: " + str};
    }
    time_t sec_int = static_cast<time_t>(seconds);
    float sec_frac = static_cast<float>(seconds - sec_int);
    t = {sec_int, sec_frac};
  } else {
    // Parse as an ISO/human-readable calendar timestamp.
    // Check if it has timezone offset (±HH:MM or Z)
    bool has_timezone = (str.find_last_of("+-Z") != std::string::npos &&
                         str.find_last_of("+-Z") > 10);

    if (has_timezone) {
      // Parse with timezone and convert to UTC
      auto [utc_time, tz_offset] = parse_time_with_timezone(str);
      t = utc_time;
      // Note: timezone offset is extracted but not returned here
      // It would need to be stored separately if needed for display
    } else {
      // No timezone, use existing parser
      t = convert_time(str);
    }
  }
}

void set_by(epoch_t &t, const std::string &str) {
  set_by(t, str, detect_timestamp_encoding(str));
}

unsigned long long parse_unsigned(const std::string &str,
                                  unsigned long long max) {
  // stoi()/stoul() both accept a leading '-' and wrap the result, so a
  // negative field used to become a huge count instead of an error.
  // Reject the sign explicitly, require the whole string to be consumed,
  // and range-check against the destination type's maximum.
  const size_t first = str.find_first_not_of(" \t");
  if (first == std::string::npos || str[first] == '-' || str[first] == '+') {
    throw std::invalid_argument{"Failed to parse an unsigned integer! " + str};
  }
  size_t pos = 0;
  unsigned long long v = 0;
  try {
    v = std::stoull(str, &pos);
  } catch (const std::exception &) {
    throw std::invalid_argument{"Failed to parse an unsigned integer! " + str};
  }
  if (pos != str.size()) {
    throw std::invalid_argument{"Failed to parse an unsigned integer! " + str};
  }
  if (v > max) {
    throw std::invalid_argument{"Unsigned integer out of range (max " +
                                std::to_string(max) + ")! " + str};
  }
  return v;
}

void set_by(unsigned &v, const std::string &str) {
  v = static_cast<unsigned>(
      parse_unsigned(str, std::numeric_limits<unsigned>::max()));
}

void set_by(double &v, const std::string &str) {
  size_t pos;
  v = stod(str, &pos);
  if (pos != str.size()) {
    throw std::invalid_argument{"Failed to parse a double precision number! " +
                                str};
  }
}

void set_by(job_queue_t &q, const std::string &str) {
  std::unordered_map<std::string, job_queue_t>::const_iterator it =
      str2jobq.find(str);
  if (it == str2jobq.cend()) {
    if (str.compare("\"\"") == 0) {
      q = QueueUnknown;
      return;
    }
    throw std::invalid_argument{"Failed to recognize a job queue string! " +
                                str};
  }
  q = it->second;
}

void set_by_queue_id(job_queue_t &q, const std::string &str) {
  if (str.empty() || !std::all_of(str.begin(), str.end(), ::isdigit)) {
    throw std::invalid_argument{"Invalid queue id: " + str};
  }
  const auto id = std::stoul(str);
  const auto max_queue_id =
#if PBATCH_GROUP
      static_cast<unsigned>(Queue10);
#else
      static_cast<unsigned>(Queue14);
#endif
  if (id == 0u || id > max_queue_id) {
    throw std::invalid_argument{"Queue id is out of range: " + str};
  }
  q = static_cast<job_queue_t>(id);
}

#if DR_EVT_LEGACY_QUEUE_INPUT
std::string to_string(const job_queue_t q) {
  std::map<job_queue_t, std::string>::const_iterator it = jobq2str.find(q);
  if (it == jobq2str.cend()) {
    throw std::invalid_argument{"Failed to recognize a job queue type!"};
  }
  return it->second;
}
#endif

/*
 * Removes leading and trailing spaces from a string
 */
std::string trim(const std::string &str, const std::string &whitespace) {
  const auto i_beg = str.find_first_not_of(whitespace);
  if (i_beg == std::string::npos)
    return ""; // no content

  const auto i_end = str.find_last_not_of(whitespace);
  const auto span = i_end - i_beg + 1;

  return str.substr(i_beg, span);
}

std::vector<substr_pos_t> comma_separate(const std::string &str) {
  std::vector<substr_pos_t> ret;
  ret.reserve(str.size());
  size_t s = 0ul;

  for (size_t i = 0ul; i < str.size(); i++) {
    if (str[i] == ',') {
      ret.emplace_back(s, i - s);
      s = i + 1;
    }
  }
  if (str.back() == ',') {
    ret.emplace_back(str.size(), 0ul);
  } else {
    ret.emplace_back(s, str.size() - s);
  }

  return ret;
}

/*
 *  Replace the comma, which is a delimiter, within a (double) quotation.
 *  Without this, parsing comma-sepated-value data may result in an error.
 *  Does not handle a case as "'...,..."' where quotation is done erroneously.
 */
void replace_comma_within_quotation(std::string &line) {
  bool db_quote_open = false; // is double quotation open
  // bool quote_open = false; // is quotation open
  bool esc = false;   // escape sequence in progress
  unsigned hash = 0u; // comment block begins
  // A comment block ends with ';'.
  // However, in some cases #include <blah.h> is shown as a non-comment
  // line. Fortunately, that will eventually ends with a C/C++ line that
  // ends with ';'

  for (auto &c : line) {
#ifdef DEBUG
    cout << c;
#endif // DEBUG
    if (c == '\\') {
      esc = (esc == false);
    } else {
      esc = false;

      if (c == '#') {
        // if (!quote_open && !db_quote_open) {
        if (!db_quote_open) {
          hash++;
        }
      } else if (c == ';') {
        // if (!quote_open && !db_quote_open) {
        if (!db_quote_open) {
          hash = 0u;
        }
      } else {
        if (hash > 0) {
          continue;
        }
        // if (c == '"' && !esc && !quote_open)  {
        if (c == '"' && !esc) {
          db_quote_open = (db_quote_open == false);
#ifdef DEBUG
          if (!db_quote_open)
            cout << endl;
#endif    // DEBUG
          /*
                          } else if (c == '\'' && !esc && !db_quote_open) {
                              quote_open = (quote_open == false);
                              // There are still errors like with record 940914,
             in which
                              // a quotation does not close in user_script
             field.
          */
#ifdef DEBUG
          // if (!quote_open && !db_quote_open) cout << endl;
          if (!db_quote_open)
            cout << endl;
#endif // DEBUG
        } else if (c == ',' && !esc) {
          // c = (db_quote_open || quote_open)? '`' : ',';
          c = (db_quote_open) ? '`' : ',';
#ifdef DEBUG
          if (c == ',')
            cout << endl;
#endif // DEBUG
        }
      }
    }
  }
#ifdef DEBUG
  cout << endl << endl;
#endif // DEBUG
}

/*
 *  Case-insensitive substring search
 */
bool search_ci(const std::string &str, const std::string &sub) {
  auto it = std::search(str.cbegin(), str.cend(), sub.cbegin(), sub.cend(),
                        [](const char c1, const char c2) {
                          return std::toupper(c1) == std::toupper(c2);
                        });
  return (it != str.cend());
}

} // end of namespace dr_evt
