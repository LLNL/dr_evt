/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file data_columns.cpp
 * @brief Trace-column format parsing and header validation implementation.
 */

#include "trace/data_columns.hpp"
#include "trace/job_record.hpp"
#include "trace/parse_utils.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace dr_evt {

Data_Columns::Data_Columns()
    : m_cur_tz(nullptr), m_total_columns(static_cast<num_cols_t>(0u)),
      m_queue_idx(static_cast<col_no_t>(0u)),
#if DR_EVT_LEGACY_QUEUE_INPUT
      m_has_queue_column(false),
#else
      m_has_q_id_column(false),
#endif
      m_col_to_avoid_idx(std::numeric_limits<col_no_t>::max()),
      m_trace_format("simple"), m_timestamp_format("epoch"),
      m_timezone_str("America/Los_Angeles"),
      m_trace_mode(TraceMode::REPLAY) // Default to replay
{
  // TODO: This should be read from an input file
  // Define the data columns to read. The rest will be not collected to
  // fill out a job record object. However, they might still be used in
  // filtering.
  m_cols_to_read = {{11, "num_nodes"}, {23, "begin_time"},
                    {24, "end_time"},  {29, "job_submit_time"},
                    {30, "queue"},     {32, "time_limit"}};
  m_col_to_avoid = "user_script";
  init();
}

Data_Columns::Data_Columns(const std::string &format)
    : m_cur_tz(nullptr), m_total_columns(static_cast<num_cols_t>(0u)),
      m_queue_idx(static_cast<col_no_t>(0u)),
#if DR_EVT_LEGACY_QUEUE_INPUT
      m_has_queue_column(false),
#else
      m_has_q_id_column(false),
#endif
      m_col_to_avoid_idx(std::numeric_limits<col_no_t>::max()),
      m_trace_format(format), m_timestamp_format("epoch"),
      m_timezone_str("America/Los_Angeles"),
      m_trace_mode(TraceMode::REPLAY) // Will be detected in check_header
{
  if (format == "simple") {
    // Simple format file columns: [job_submit_time, begin_time, end_time,
    // num_nodes, exit_status, queue, time_limit, actual_run_time (optional)]
    // Job_Record expects: [num_nodes, begin_time, end_time, job_submit_time,
    // queue, time_limit, actual_run_time] After sorting by column index, we
    // need to remap to Job_Record order We define in Job_Record's expected
    // order here, but need different column indices:
    m_cols_to_read = {
        {3, "num_nodes"},       // File column 3 -> Job_Record field 0
        {1, "begin_time"},      // File column 1 -> Job_Record field 1
        {2, "end_time"},        // File column 2 -> Job_Record field 2
        {0, "job_submit_time"}, // File column 0 -> Job_Record field 3
        {5, "queue"},           // File column 5 -> Job_Record field 4
        {6, "time_limit"},      // File column 6 -> Job_Record field 5
        {7, "actual_run_time"} // File column 7 -> Job_Record field 6 (optional)
    };
    m_col_to_avoid = ""; // No problematic columns in simple format
  } else if (format == "swf") {
    // Standard Workload Format: no header row and whitespace-separated
    // fields, parsed by Swf_Trace_Policy::load_records(). This list only
    // names the canonical fields that loader fills; the indices are unused.
    m_cols_to_read = {{4, "num_nodes"},
                      {1, "job_submit_time"},
                      {14, "q_id"},
                      {8, "time_limit"},
                      {3, "actual_run_time"}};
    m_col_to_avoid = "";
  } else {
    // Lassen format (default)
    m_cols_to_read = {{11, "num_nodes"}, {23, "begin_time"},
                      {24, "end_time"},  {29, "job_submit_time"},
                      {30, "queue"},     {32, "time_limit"}};
    m_col_to_avoid = "user_script";
  }
  init();
}

Data_Columns::Data_Columns(const std::string &format,
                           const std::string &timestamp_format,
                           const std::string &timezone)
    : m_cur_tz(nullptr), m_total_columns(static_cast<num_cols_t>(0u)),
      m_queue_idx(static_cast<col_no_t>(0u)),
#if DR_EVT_LEGACY_QUEUE_INPUT
      m_has_queue_column(false),
#else
      m_has_q_id_column(false),
#endif
      m_col_to_avoid_idx(std::numeric_limits<col_no_t>::max()),
      m_trace_format(format), m_timestamp_format(timestamp_format),
      m_timezone_str(timezone),
      m_trace_mode(TraceMode::REPLAY) // Will be detected in check_header
{
  if (format == "simple") {
    // Simple format: [arrival_time, start_time, end_time, num_nodes,
    // exit_status, queue, time_limit, actual_run_time (optional)]
    m_cols_to_read = {{3, "num_nodes"},      {1, "begin_time"},
                      {2, "end_time"},       {0, "job_submit_time"},
                      {5, "queue"},          {6, "time_limit"},
                      {7, "actual_run_time"}};
    m_col_to_avoid = "";
  } else if (format == "swf") {
    // Standard Workload Format: no header row and whitespace-separated
    // fields, parsed by Swf_Trace_Policy::load_records(). This list only
    // names the canonical fields that loader fills; the indices are unused.
    m_cols_to_read = {{4, "num_nodes"},
                      {1, "job_submit_time"},
                      {14, "q_id"},
                      {8, "time_limit"},
                      {3, "actual_run_time"}};
    m_col_to_avoid = "";
  } else {
    // Lassen format
    m_cols_to_read = {{11, "num_nodes"}, {23, "begin_time"},
                      {24, "end_time"},  {29, "job_submit_time"},
                      {30, "queue"},     {32, "time_limit"}};
    m_col_to_avoid = "user_script";
  }
  init();
}

Data_Columns::~Data_Columns() {
  // Restore the original timezone
  if (m_cur_tz != nullptr) {
    setenv("TZ", m_cur_tz, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  if (m_cur_tz != nullptr) {
    std::free(m_cur_tz);
    m_cur_tz = nullptr;
  }
}

void Data_Columns::init() {
  for (auto i = static_cast<num_cols_t>(0u); i < m_cols_to_read.size(); ++i) {
    const auto &c = m_cols_to_read[i];
    const auto result = m_col_by_name.insert(
        std::make_pair(c.second, std::make_pair(c.first, i)));

    if (!result.second) {
      std::string err("Possible duplicate column name with " + c.second);
      throw std::invalid_argument{err.c_str()};
    }
#if DR_EVT_LEGACY_QUEUE_INPUT
    if (c.second == "queue") {
#else
    if (c.second == "q_id") {
#endif
      m_queue_idx = i;
    }
  }

  // Make sure the columns are in the order of increasing index
  // NOTE: For simple format, we keep them in Job_Record's expected order, not
  // file order
  if (m_trace_format != "simple") {
    std::sort(m_cols_to_read.begin(), m_cols_to_read.end());
  }

  Job_Record::set_num_inputs(static_cast<unsigned>(size()));

  // Set timezone to the zone where data was collected.
  // This will be used in converting time strings to determine
  // the daylight saving condition.

  if (m_cur_tz != nullptr) {
    std::free(m_cur_tz);
    m_cur_tz = nullptr;
  }

  const char *tz = getenv("TZ");
  if (tz != nullptr) {
    auto tz_str_len = strlen(tz);
    m_cur_tz = (char *)calloc((tz_str_len + 1), sizeof(char));
    memcpy((void *)m_cur_tz, (void *)tz, tz_str_len * sizeof(char));
  }

  setenv("TZ", m_timezone_str.c_str(), 1);
  tzset();
}

bool Data_Columns::check_header(const std::string &fname) {
  if (fname.empty()) {
    return false;
  }
  if (m_trace_format == "swf") {
    // SWF files carry no column header; Swf_Trace_Policy::load_records()
    // parses the ';' metadata lines itself. SWF describes submissions only
    // (no begin/end times), so this is always simulation mode, and the
    // loader assigns every record a queue id.
    m_trace_mode = TraceMode::SIMULATION;
#if DR_EVT_LEGACY_QUEUE_INPUT
    m_has_queue_column = true;
#else
    m_has_q_id_column = true;
#endif
    return true;
  }

  std::ifstream ifs(fname);
  if (!ifs) {
    return false;
  }

  if (m_cols_to_read.empty()) {
    return true;
  }

  std::string line;
  std::getline(ifs, line); // Read the header line
  // Drop a UTF-8 byte-order mark (EF BB BF). Excel and some Windows editors
  // write one; left in place it becomes part of the first column name and
  // the required column is reported as missing.
  if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
      static_cast<unsigned char>(line[1]) == 0xBB &&
      static_cast<unsigned char>(line[2]) == 0xBF) {
    line.erase(0, 3);
  }
  std::istringstream header(line);
  std::vector<std::string> col_names;

  auto idx = static_cast<col_no_t>(0u);
  while (header.good()) { // Find the number of columns
    std::string substr;
    std::getline(header, substr, ',');

    // Strip trailing whitespace (including \r from Windows line endings)
    while (!substr.empty() &&
           (substr.back() == ' ' || substr.back() == '\t' ||
            substr.back() == '\r' || substr.back() == '\n')) {
      substr.pop_back();
    }

    col_names.emplace_back(substr);

    if (substr == m_col_to_avoid) {
      m_col_to_avoid_idx = idx;
      std::cerr << "Avoid parsing " + substr << std::endl;
    }
    idx++;
  }

  // Build column name to index map from actual header
  std::map<std::string, col_no_t> col_map;
  for (col_no_t i = 0; i < col_names.size(); ++i) {
    col_map[col_names[i]] = i;
  }

  // Detect trace mode from columns present
  bool has_begin_time = col_map.find("begin_time") != col_map.end();
  bool has_end_time = col_map.find("end_time") != col_map.end();

  if (has_begin_time && has_end_time) {
    m_trace_mode = TraceMode::REPLAY;
  } else if (!has_begin_time && !has_end_time) {
    m_trace_mode = TraceMode::SIMULATION;
  } else {
    throw std::invalid_argument(
        "Ambiguous trace format: has one of begin_time/end_time but not both");
  }

  // Submission time, time_limit, and actual_run_time accept column-name
  // aliases, so an existing trace can be reused without editing its
  // header (which is slow to do by hand on a large file). Only one of
  // each alias set is expected to actually be present; if more than one
  // is, the first match in this list wins.
  auto find_column =
      [&col_map](const std::vector<std::string> &names) -> col_no_t {
    for (const auto &name : names) {
      auto it = col_map.find(name);
      if (it != col_map.end()) {
        return it->second;
      }
    }
    throw std::invalid_argument(
        "Required column '" + names.front() +
        "' (or an accepted alias) not found in trace file");
  };
  auto find_column_optional =
      [&col_map](
          const std::vector<std::string> &names) -> std::pair<bool, col_no_t> {
    for (const auto &name : names) {
      auto it = col_map.find(name);
      if (it != col_map.end()) {
        return {true, it->second};
      }
    }
    return {false, 0};
  };

  static const std::vector<std::string> time_limit_aliases = {
      "time_limit", "timelimit", "walltime"};
  static const std::vector<std::string> submit_time_aliases = {
      "job_submit_time", "submit_time"};
  static const std::vector<std::string> actual_run_time_aliases = {
      "actual_run_time", "actual_runtime", "duration", "actual_duration",
      "run_time"};

  // Rebuild m_cols_to_read with actual column indices from header
  m_cols_to_read.clear();

#if DR_EVT_LEGACY_QUEUE_INPUT
  m_has_queue_column = col_map.find("queue") != col_map.end();
#else
  // Queue names are deliberately ignored in ID mode. q_id is optional,
  // just as queue is in legacy mode; an absent value defaults to Queue1.
  m_has_q_id_column = col_map.find("q_id") != col_map.end();
#endif

  if (m_trace_mode == TraceMode::REPLAY) {
    // Replay mode: need all columns including begin_time and end_time
    m_cols_to_read = {{find_column({"num_nodes"}), "num_nodes"},
                      {find_column({"begin_time"}), "begin_time"},
                      {find_column({"end_time"}), "end_time"},
                      {find_column(submit_time_aliases), "job_submit_time"},
                      {find_column(time_limit_aliases), "time_limit"}};
#if DR_EVT_LEGACY_QUEUE_INPUT
    if (m_has_queue_column) {
      m_cols_to_read.insert(m_cols_to_read.end() - 1,
                            {find_column({"queue"}), "queue"});
    }
#else
    if (m_has_q_id_column) {
      m_cols_to_read.insert(m_cols_to_read.end() - 1,
                            {find_column({"q_id"}), "q_id"});
    }
#endif

    // A replay trace does not require actual_run_time because it can be
    // derived from begin_time/end_time. When supplied, retain it so the row
    // loader can verify that all three observed execution fields agree.
    auto [found, actual_run_time_idx] =
        find_column_optional(actual_run_time_aliases);
    if (found) {
      m_cols_to_read.push_back({actual_run_time_idx, "actual_run_time"});
    }
  } else {
    // Simulation mode: no begin_time or end_time
    col_no_t num_nodes_idx = find_column({"num_nodes"});
    col_no_t submit_time_idx = find_column(submit_time_aliases);
    col_no_t time_limit_idx = find_column(time_limit_aliases);

    m_cols_to_read = {{num_nodes_idx, "num_nodes"},
                      {submit_time_idx, "job_submit_time"},
                      {time_limit_idx, "time_limit"}};
#if DR_EVT_LEGACY_QUEUE_INPUT
    if (m_has_queue_column) {
      m_cols_to_read.insert(m_cols_to_read.end() - 1,
                            {find_column({"queue"}), "queue"});
    }
#else
    if (m_has_q_id_column) {
      m_cols_to_read.insert(m_cols_to_read.end() - 1,
                            {find_column({"q_id"}), "q_id"});
    }
#endif

    // Optional: actual_run_time column for RunTimeMode::ACTUAL
    auto [found, actual_run_time_idx] =
        find_column_optional(actual_run_time_aliases);
    if (found) {
      m_cols_to_read.push_back({actual_run_time_idx, "actual_run_time"});
    }
  }
  // No further validation needed here: find_column already throws with a
  // clear message (naming the canonical column and its accepted aliases)
  // if a required column - under any of its accepted names - is absent.
  m_total_columns = static_cast<num_cols_t>(col_names.size());

  // Sort columns by file index for proper extraction
  // Exception: for simple format, keep in Job_Record expected order
  if (m_trace_format != "simple") {
    std::sort(m_cols_to_read.begin(), m_cols_to_read.end());
  }

  // Reinitialize after rebuilding m_cols_to_read
  m_col_by_name.clear();

  for (auto i = static_cast<num_cols_t>(0u); i < m_cols_to_read.size(); ++i) {
    const auto &c = m_cols_to_read[i];
    const auto result = m_col_by_name.insert(
        std::make_pair(c.second, std::make_pair(c.first, i)));

    if (!result.second) {
      std::string err("Possible duplicate column name with " + c.second);
      throw std::invalid_argument{err.c_str()};
    }
#if DR_EVT_LEGACY_QUEUE_INPUT
    if (c.second == "queue") {
#else
    if (c.second == "q_id") {
#endif
      m_queue_idx = i;
    }
  }

  // Update Job_Record with correct field count
  // The loader supplies Queue1 as the default when the selected field
  // is absent, so Job_Record always receives its canonical field layout.
#if DR_EVT_LEGACY_QUEUE_INPUT
  Job_Record::set_num_inputs(static_cast<unsigned>(m_cols_to_read.size()) +
                             (m_has_queue_column ? 0u : 1u));
#else
  Job_Record::set_num_inputs(static_cast<unsigned>(m_cols_to_read.size()) +
                             (m_has_q_id_column ? 0u : 1u));
#endif

  return true;
}

/*
 *  This function is data-specific.
 *  The Lassen trace file contains for example 'user_script' field, which is
 *  23rd. This field contains many characters that would make parsing difficult.
 *  e.g., ',', ';', '"', "'", '\' and '#'.
 *  Especially, if it contains ','. There are more fields that sometimes
 *  contain such characters in their values as well.
 *  Therefore, we first check the number of comma-separated values in a line.
 *  If it is larger than expected, we try removing commas in values.
 *  Then, we detect the position of values of the columns after 'user_script'
 *  from the back of the string towards the problematic one.
 */
std::vector<substr_pos_t>
Data_Columns::pick_values(const std::string &str) const {
  auto col_pos = comma_separate(str);
  // col_pos.size() should not be less than m_total_columns
  // If it is larger, it is due to the non-delimiter commas in a value.
  if (col_pos.size() < m_total_columns) {
    std::string err_str = "The number of comma-separated values are "
                          "less than the total number of columns: " +
                          std::to_string(col_pos.size()) + " < " +
                          std::to_string(m_total_columns);
    throw std::length_error(err_str);
  }

  if (col_pos.size() > m_total_columns) {
    auto str_cpy = str;
    replace_comma_within_quotation(str_cpy);
    col_pos = comma_separate(str_cpy);
  }

  const auto sz = static_cast<num_cols_t>(m_cols_to_read.size());
  col_no_t i = static_cast<col_no_t>(0u);
  std::vector<substr_pos_t> val_pos(sz);

  for (; i < sz; ++i) {
    const auto &c = m_cols_to_read[i];
    if (c.first >= m_col_to_avoid_idx)
      break;
    val_pos[i] = col_pos[c.first];
  }

  for (col_no_t j = sz; i < j;) {
    const auto &c = m_cols_to_read[--j];
    val_pos[j] = col_pos[c.first];
  }

  return val_pos;
}

col_no_t Data_Columns::column_idx_raw(const std::string &col_name) const {
  const auto &it = m_col_by_name.find(col_name);
  if (it == m_col_by_name.cend()) {
    std::string err = "Unknown column name: " + col_name;
    throw std::invalid_argument{err.c_str()};
  }
  return it->second.first;
}

col_no_t Data_Columns::column_idx(const std::string &col_name) const {
  const auto &it = m_col_by_name.find(col_name);
  if (it == m_col_by_name.cend()) {
    std::string err = "Unknown column name: " + col_name;
    throw std::invalid_argument{err.c_str()};
  }
  return it->second.second;
}

} // end of namespace dr_evt
