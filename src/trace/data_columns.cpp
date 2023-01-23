/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include "trace/data_columns.hpp"
#include "trace/job_record.hpp"
#include "trace/parse_utils.hpp"

namespace dr_evt {

Data_Columns::Data_Columns()
  : m_cur_tz(nullptr),
    m_total_columns(static_cast<num_cols_t>(0u)),
    m_queue_idx(static_cast<col_no_t>(0u))
{
    // TODO: This should be read from an input file
    // Define the data columns to read. The rest will be not collected to
    // fill out a job record object. However, they might still be used in
    // filtering.
    m_cols_to_read = {
        {11, "num_nodes"}, {23, "begin_time"}, {24, "end_time"},
        {29, "job_submit_time"}, {30, "queue"}, {32, "time_limit"}
    };
    m_col_to_avoid = "user_script";
    init();
}

Data_Columns::~Data_Columns()
{
    // Restore the original timezone
     if (m_cur_tz != nullptr) {
        setenv("TZ", m_cur_tz, 1);
     } else {
        unsetenv("TZ");
     }
    tzset();
    if (m_cur_tz != nullptr) {
        delete m_cur_tz;
        m_cur_tz = nullptr;
    }
}

void Data_Columns::init()
{
    for (auto i = static_cast<num_cols_t>(0u); i < m_cols_to_read.size(); ++i) {
        const auto& c = m_cols_to_read[i];
        const auto result
            = m_col_by_name.insert(
                  std::make_pair(c.second,
                                 std::make_pair(c.first, i)));

        if (!result.second) {
            std::string err("Possible duplicate column name with " + c.second);
            throw std::invalid_argument {err.c_str()};
        }
        if (c.second == "queue") {
            m_queue_idx = i;
        }
    }

    // Make sure the colums are in the order of increasing index
    std::sort(m_cols_to_read.begin(), m_cols_to_read.end());

    Job_Record::set_num_inputs(static_cast<unsigned>(size()));

    // Set timezone to the zone where data was collected.
    // This will be used in converting time strings to determine
    // the daylight saving condition.

    if (m_cur_tz != nullptr) {
        delete m_cur_tz;
        m_cur_tz = nullptr;
    }

    const char* tz = getenv("TZ");
    if (tz != nullptr) {
        auto tz_str_len = strlen(tz);
        m_cur_tz = (char*) calloc((tz_str_len + 1), sizeof(char));
        memcpy((void*) m_cur_tz, (void*) tz, tz_str_len * sizeof(char));
    }

    setenv("TZ", DATA_TIMEZONE, 1);
    tzset();
}

bool Data_Columns::check_header(const std::string& fname)
{
    if (fname.empty()) {
        return false;
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
    std::istringstream header(line);
    std::vector<std::string> col_names;

    auto idx = static_cast<col_no_t>(0u);
    while (header.good()) { // Find the number of columns
        std::string substr;
        std::getline(header, substr, ',');
        col_names.emplace_back(substr);

        if (substr == m_col_to_avoid) {
            m_col_to_avoid_idx = idx;
            std::cerr << "Avoid parsing " + substr << std::endl;
        }
        idx ++;
    }

    for (const auto& c: m_cols_to_read) {
        if (col_names.at(c.first) != c.second) {
            std::string err_str =
                "Column <" + std::to_string(c.first) + ' ' + c.second +
                "> is not present!";
            throw std::invalid_argument {err_str};
            return false;
        }
    }
    m_total_columns = static_cast<num_cols_t>(col_names.size());

    return true;
}

/**
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
Data_Columns::pick_values(const std::string& str) const
{
    auto col_pos = comma_separate(str);
    // col_pos.size() should not be less than m_total_columns
    // If it is larger, it is due to the non-delimiter commas in a value.
    if (col_pos.size() < m_total_columns) {
        std::string err_str
            = "The number of comma-separated values are "
              "less than the total number of columns: "
            + std::to_string(col_pos.size())
            + " < " + std::to_string(m_total_columns);
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
        const auto& c = m_cols_to_read[i];
        if (c.first >= m_col_to_avoid_idx) break;
        val_pos[i] = col_pos[c.first];
    }

    for (col_no_t j = sz; i < j; ) {
        const auto& c = m_cols_to_read[--j];
        val_pos[j] = col_pos[c.first];
    }

    return val_pos;
}

col_no_t Data_Columns::column_idx_raw(const std::string& col_name) const
{
    const auto& it = m_col_by_name.find(col_name);
    if (it == m_col_by_name.cend()) {
        std::string err = "Unknown column name: " + col_name;
        throw std::invalid_argument {err.c_str()};
    }
    return it->second.first;
}

col_no_t Data_Columns::column_idx(const std::string& col_name) const
{
    const auto& it = m_col_by_name.find(col_name);
    if (it == m_col_by_name.cend()) {
        std::string err = "Unknown column name: " + col_name;
        throw std::invalid_argument {err.c_str()};
    }
    return it->second.second;
}

} // end of namespace dr_evt
