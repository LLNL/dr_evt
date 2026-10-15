/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file job_io.cpp
 * @brief Job-trace input and diagnostic output implementation.
 */

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "trace/job_io.hpp"
#include "trace/parse_utils.hpp"

namespace dr_evt {

using std::cout;
using std::endl;
using std::string;
using std::vector;

#if DR_EVT_LEGACY_QUEUE_INPUT
int load(const string &fname, const Data_Columns &dcols,
         vector<Job_Record> &data, num_jobs_t max_cnt) {
  if (fname.empty()) {
    return EXIT_FAILURE;
  }

  std::ifstream ifs(fname);
  if (!ifs) {
    return EXIT_FAILURE;
  }

  const auto &columns_to_read = dcols.get_cols_to_read();
  const auto record_sz = columns_to_read.size();
  const bool has_queue_column = dcols.has_queue_column();
#if !SHOW_ALL_QUEUE
  const auto q_idx = dcols.get_queue_idx();
#endif

  if (columns_to_read.empty()) {
    std::cerr << "no column to read!" << std::endl;
    return EXIT_SUCCESS;
  }

  string line;
  std::getline(ifs, line); // Consume the header line

  if (max_cnt == static_cast<num_jobs_t>(0u)) {
    max_cnt = std::numeric_limits<num_jobs_t>::max();
  }
  num_jobs_t cnt = static_cast<num_jobs_t>(0u);
  TimestampEncoding timestamp_encoding = TimestampEncoding::EPOCH;
  bool timestamp_encoding_detected = false;

  while (std::getline(ifs, line)) { // Read a line
    // Strip trailing whitespace (including \r from Windows line endings)
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                             line.back() == '\r' || line.back() == '\n')) {
      line.pop_back();
    }
    // A blank line (commonly a trailing newline at end of file) is not a
    // record: skip it instead of failing the load on a short field count.
    // It does not count toward max_cnt either.
    if (line.empty()) {
      continue;
    }
    if (cnt++ >= max_cnt) {
      break;
    }

    vector<string> rec_str;
    rec_str.reserve(record_sz);

    auto val_pos = dcols.pick_values(line);
#if !SHOW_ALL_QUEUE
    // A queue-less trace is assigned pbatch below, so it is always in
    // the queue of interest under the default filtering configuration.
    bool right_q = !has_queue_column; // queue of interest
#endif                                // !SHOW_ALL_QUEUE

    for (auto col_idx = static_cast<col_no_t>(0u); col_idx < record_sz;
         col_idx++) { // Check the job queue
      const auto &pos = val_pos[col_idx];
      string substr = line.substr(pos.first, pos.second);
#if !SHOW_ALL_QUEUE
      if (has_queue_column && col_idx == q_idx) { // Check the job queue
#if INCLUDE_DAT || MARK_DAT_PERIOD
        bool q1 = false, q2 = false;
#if 0 // Case-insensitive search
                right_q = (q1 = search_ci(substr, "pbatch")) ||
                          (q2 = search_ci(substr, "pall"));
#else
        right_q = (q1 = (substr.find("pbatch") != string::npos)) ||
                  (q2 = (substr.find("pall") != string::npos));
#endif
        if (!right_q) {
          break;
        }
#else
        right_q = search_ci(substr, "pbatch");
        if (!right_q)
          break;
#endif
      }
#endif // !SHOW_ALL_QUEUE
      rec_str.emplace_back(trim(substr));
    }

#if !SHOW_ALL_QUEUE
    if (!right_q) {
      continue;
    }
#endif // !SHOW_ALL_QUEUE

    // Job_Record has one canonical layout in each mode. Keep the omitted
    // source field out of the output schema, but insert its default.
    if (!has_queue_column) {
      const auto queue_pos =
          dcols.get_trace_mode() == TraceMode::REPLAY ? 4u : 2u;
      rec_str.insert(rec_str.begin() + queue_pos, "pbatch");
    }

    if (!timestamp_encoding_detected) {
      timestamp_encoding = detect_timestamp_encoding(rec_str.at(1));
      timestamp_encoding_detected = true;
    }

    try {
      // Constructor may raise an exception based on filtering.
      // In that case, it can be handled as below to ignore this
      // particular sample that is not compliant.
#if SHOW_ORG_NO
      // line number starts from 1
      data.push_back(Job_Record(cnt, rec_str, timestamp_encoding));
#else
      data.push_back(Job_Record(rec_str, timestamp_encoding));
#endif
    } catch (std::domain_error &e) {
      // Ignore this case
      std::cerr << std::string(e.what()) + ": [" + std::to_string(cnt) + "]"
                << endl;
    } catch (std::exception &e) {
      std::ostringstream oss_err;
      oss_err << e.what() << ": [" << cnt << "] " << line << endl;
      throw std::invalid_argument{oss_err.str().c_str()};
    }
  }
  ifs.close();

  return EXIT_SUCCESS;
}
#else
/** @brief Load CSV trace rows into job records using configured columns.
 * @param[in] fname Input trace filename.
 * @param[in] dcols Validated input-column mapping.
 * @param[out] data Destination records appended from the file.
 * @param[in] max_cnt Maximum records to load; zero means no explicit limit.
 * @return `EXIT_SUCCESS` on success, otherwise a nonzero status. */
int load(const string &fname, const Data_Columns &dcols,
         vector<Job_Record> &data, num_jobs_t max_cnt) {
  if (fname.empty()) {
    return EXIT_FAILURE;
  }
  std::ifstream ifs(fname);
  if (!ifs) {
    return EXIT_FAILURE;
  }

  const auto &columns_to_read = dcols.get_cols_to_read();
  const auto record_sz = columns_to_read.size();
  if (columns_to_read.empty()) {
    std::cerr << "no column to read!" << std::endl;
    return EXIT_SUCCESS;
  }

  string line;
  std::getline(ifs, line); // Consume the header line.
  if (max_cnt == static_cast<num_jobs_t>(0u)) {
    max_cnt = std::numeric_limits<num_jobs_t>::max();
  }
  TimestampEncoding timestamp_encoding = TimestampEncoding::EPOCH;
  bool timestamp_encoding_detected = false;

  if (dcols.has_q_id_column()) {
    const auto q_idx = dcols.get_queue_idx();
    num_jobs_t cnt = static_cast<num_jobs_t>(0u);
    while (std::getline(ifs, line)) {
      while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                               line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
      }
      if (line.empty()) {
        continue; // blank line, see load_data()
      }
      if (cnt++ >= max_cnt) {
        break;
      }

      vector<string> fields;
      fields.reserve(record_sz - 1u);
      job_queue_t queue = Queue1;
      try {
        const auto val_pos = dcols.pick_values(line);
        for (auto col_idx = static_cast<col_no_t>(0u); col_idx < record_sz;
             ++col_idx) {
          const auto &pos = val_pos[col_idx];
          string value = trim(line.substr(pos.first, pos.second));
          if (col_idx == q_idx) {
            set_by_queue_id(queue, value);
            continue;
          }
          fields.emplace_back(std::move(value));
        }
        if (!timestamp_encoding_detected) {
          timestamp_encoding = detect_timestamp_encoding(fields.at(1));
          timestamp_encoding_detected = true;
        }
        data.emplace_back(fields, queue,
                          dcols.get_trace_mode() == TraceMode::REPLAY,
                          timestamp_encoding);
#if SHOW_ORG_NO
        data.back().set_org_line_no(cnt);
#endif
      } catch (std::domain_error &e) {
        std::cerr << std::string(e.what()) + ": [" + std::to_string(cnt) + "]"
                  << endl;
      } catch (std::exception &e) {
        std::ostringstream oss_err;
        oss_err << e.what() << ": [" << cnt << "] " << line << endl;
        throw std::invalid_argument{oss_err.str().c_str()};
      }
    }
    return EXIT_SUCCESS;
  }

  // This queue-free path has no queue-ID index, conversion, or condition.
  num_jobs_t cnt = static_cast<num_jobs_t>(0u);
  while (std::getline(ifs, line)) {
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                             line.back() == '\r' || line.back() == '\n')) {
      line.pop_back();
    }
    if (line.empty()) {
      continue; // blank line, see load_data()
    }
    if (cnt++ >= max_cnt) {
      break;
    }

    vector<string> fields;
    fields.reserve(record_sz);
    try {
      const auto val_pos = dcols.pick_values(line);
      for (auto col_idx = static_cast<col_no_t>(0u); col_idx < record_sz;
           ++col_idx) {
        const auto &pos = val_pos[col_idx];
        fields.emplace_back(trim(line.substr(pos.first, pos.second)));
      }
      if (!timestamp_encoding_detected) {
        timestamp_encoding = detect_timestamp_encoding(fields.at(1));
        timestamp_encoding_detected = true;
      }
      data.emplace_back(fields, Queue1,
                        dcols.get_trace_mode() == TraceMode::REPLAY,
                        timestamp_encoding);
#if SHOW_ORG_NO
      data.back().set_org_line_no(cnt);
#endif
    } catch (std::domain_error &e) {
      std::cerr << std::string(e.what()) + ": [" + std::to_string(cnt) + "]"
                << endl;
    } catch (std::exception &e) {
      std::ostringstream oss_err;
      oss_err << e.what() << ": [" << cnt << "] " << line << endl;
      throw std::invalid_argument{oss_err.str().c_str()};
    }
  }
  return EXIT_SUCCESS;
}
#endif // DR_EVT_LEGACY_QUEUE_INPUT

/** @brief Print requested limits beside actual execution durations.
 * @param[in] data Job records to print. */
void print_limit_vs_exec_time(const vector<Job_Record> &data) {
  cout << "limit" << "\t" << "exec" << endl;

  using lim_exec_t = std::pair<timeout_t, tdiff_t>;
  vector<lim_exec_t> data_focus(data.size());

  for (num_jobs_t i = static_cast<num_jobs_t>(0u); i < data.size(); ++i) {
    const auto &job = data[i];
    data_focus[i] =
        std::make_pair(job.get_limit_time(), job.get_actual_run_time());
  }

  std::stable_sort(
      data_focus.begin(), data_focus.end(),
      [](const lim_exec_t &lhs, const lim_exec_t &rhs) -> bool {
        return ((lhs.first < rhs.first) ||
                ((lhs.first == rhs.first) && (lhs.second < rhs.second)));
      });

  for (const auto &job : data_focus) {
    cout << job.first << '\t' << job.second << endl;
  }
}

} // end of namespace dr_evt
