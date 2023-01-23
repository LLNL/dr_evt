/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_PARAMS_TRACE_PARAMS_HPP
#define DR_EVT_PARAMS_TRACE_PARAMS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#include <string>
#include "dr_evt_types.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_params
 *  @{ */

class Trace_Params {
 public:
    Trace_Params();
    bool getopt(int& argc, char** &argv);
    void print_usage(const std::string exec, int code);
    void print() const;
    std::string get_infile() const { return m_infile; }
    void set_outfile(const std::string& ofname);
    std::string get_outfile() const { return m_outfile; }
    std::string get_datfile() const { return m_datfile; }
    std::string get_subfile() const { return m_subfile; }
    std::string get_subsumfile() const { return m_subsumfile; }

    num_jobs_t max_num_jobs() const { return m_max_jobs; }
    bool is_max_jobs_set() const { return m_is_jobs_set; }

    num_jobs_t m_max_jobs;
    std::string m_max_time;

    std::string m_infile;
    std::string m_outfile;
    std::string m_datfile; ///< Outfile name for detected DAT sessions
    std::string m_subfile; ///< Outfile name for Submission stats
    std::string m_subsumfile; ///< Outfile name for submission stat summary

    bool m_is_jobs_set;
    bool m_is_time_set;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_PARAMS_TRACE_PARAMS_HPP
