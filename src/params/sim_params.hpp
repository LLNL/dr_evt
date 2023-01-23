/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_PARAMS_SIM_PARAMS_HPP
#define DR_EVT_PARAMS_SIM_PARAMS_HPP

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

class Sim_Params {
 public:
    Sim_Params();
    void getopt(int& argc, char** &argv);
    void print_usage(const std::string exec, int code);
    void print() const;
    void set_outfile(const std::string& ofname);
    std::string get_outfile() const;

    unsigned m_seed;
    dr_evt::num_jobs_t m_max_jobs;
    dr_evt::sim_time_t m_max_time;

    std::string m_infile;

    bool m_is_jobs_set;
    bool m_is_time_set;

 private:
    std::string m_outfile;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_PARAMS_SIM_PARAMS_HPP
