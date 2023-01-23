/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_DATA_COLUMNS_HPP
#define DR_EVT_TRACE_DATA_COLUMNS_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include "common.hpp"
#include "trace/column_id.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

class Data_Columns {
  public:
    using data_columns_t = std::vector<column_id_t>;

  protected:
    /// Map a name to the column index in the raw data and that in the filtered
    using col_by_name_t = std::unordered_map<std::string,
                                             std::pair<col_no_t, col_no_t>>;

    /**
     * Column filter defines the columns to read.
     * The rest will be filtered out.
     */
    data_columns_t m_cols_to_read;

    /// maps a column name to an index to the filter
    col_by_name_t m_col_by_name;

    /**
     * The current timezone is backed up before processing and restored after.
     * The timezone information is needed to determine daylight saving in case
     * that the timestamps in data are missing timezone info.
     * In that case, the timezone of the data needs to be set as the macro value
     * DATA_TIMEZONE in common.h
     */
    const char* m_cur_tz;

    /// Total number of columns in the data file checked against
    num_cols_t m_total_columns;

    /// The index of the column_id entry for queue
    col_no_t m_queue_idx;

    /// A particular column that is extrememly difficult to parse.
    std::string m_col_to_avoid;
    /// Column index of the m_col_to_avoid in the raw data
    col_no_t m_col_to_avoid_idx;

  public:
    Data_Columns();
    virtual ~Data_Columns();

    /// Allow read-only access to the column filter
    const data_columns_t& get_cols_to_read() const { return m_cols_to_read; }

    /**
     *  Check if the column filter is consistent with the header of data file.
     *  It also detects the total number of columns.
     */
    bool check_header(const std::string& fname);

    /// Return the number of columns
    num_cols_t size() const { return static_cast<num_cols_t>(m_cols_to_read.size()); }

    /// Select the substring ranges that matches the columns of interest
    virtual std::vector<substr_pos_t> pick_values(const std::string& str) const;

    /// Return the index of the column in raw data by name
    col_no_t column_idx_raw(const std::string& col_name) const;

    col_no_t column_idx(const std::string& col_name) const;

    col_no_t get_queue_idx() const { return m_queue_idx; }

  protected:
    void init();
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_DATA_COLUMNS_HPP
