/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_TRACE_HPP
#define DR_EVT_TRACE_TRACE_HPP

#include <cstdlib>
#include <vector>
#include <string>
#include <iostream>

#include "common.hpp"
#include "trace/data_columns.hpp"
#include "trace/job_record.hpp"
#include "trace/dr_event.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

class Trace {
  public:
    using trace_data_t = std::vector<Job_Record>;
    using reserved_t = std::vector<period_t>;

  protected:
    const std::string m_fname; ///< Name of the input datafile
    Data_Columns m_dcols; ///< Header info and column filter
    trace_data_t m_data; ///< Job trace data
  #if MARK_DAT_PERIOD
    /// Period where resources were unavailable for batch jobs
    reserved_t m_reserved;
  #endif

    /// Tracing context, i.e., temporary data while running simulation
    struct Context {
      #if MARK_DAT_PERIOD
        num_jobs_t m_pAll_cnt; // On-going pAll job
        epoch_t m_dat_start;
        epoch_t m_dat_end;
        tdiff_t m_dat_span;
        job_queue_t m_prev_job_q;
      #endif
        num_nodes_t m_n_nodes_in_use;
        event_q_t m_evtq;

        Context();
        std::string to_string() const;
    };

  public:
    Trace(const std::string& fname);

    /// Allow access to the header info and column filter
    const Data_Columns& dcols() const { return m_dcols; }

    /// Load job trace data from a file
    int load_data(num_jobs_t n_lines_to_read = static_cast<num_jobs_t>(0u));

    /// Allow write access to the job trace data
    trace_data_t& data() { return m_data; }
    /// Allow read-only access to the job trace data
    const trace_data_t& data() const { return m_data; }

    /**
     *  Run the trace from the begining to the end. i.e., run the simulation
     *  of 3 job events--submit, start, and end--in order to find out how many
     *  nodes were in use at the time of each job submission.
     */
    void run_job_trace();
    /**
     *  Print out the job trace with extra information obtained from simulation.
     */
    std::ostream& print(std::ostream& os) const;

    /// Print out the total span of time of the trace
    std::ostream& print_span(std::ostream& os) const;

  #if MARK_DAT_PERIOD
    std::ostream& print_DAT(std::ostream& os);
  #endif

  #if MARK_DAT_PERIOD
    const reserved_t & get_reserved() const { return m_reserved; }
  #endif

  protected:
    void process_events_until(Context& ctx, const epoch_t& t_sub);
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_TRACE_HPP
