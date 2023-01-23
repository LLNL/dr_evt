#ifndef DR_EVT_TRACE_JOB_IO_HPP
#define DR_EVT_TRACE_JOB_IO_HPP

#include <string>
#include <vector>
#include <iostream>
#include "common.hpp"
#include "trace/column_id.hpp"
#include "trace/data_columns.hpp"
#include "trace/job_record.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

int load(const std::string& fname,
          const Data_Columns& dcols,
          std::vector<Job_Record>& data,
          num_jobs_t max_cnt = static_cast<num_jobs_t>(0u));

std::ostream& print(std::ostream& os, const std::vector<Job_Record>& data);

void print_limit_vs_exec_time(const std::vector<Job_Record>& data);

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_IO_HPP
