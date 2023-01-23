/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_PARAMS_DR_EVT_PARAMS_HPP
#define DR_EVT_PARAMS_DR_EVT_PARAMS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
//#error "no config"
#endif

namespace dr_evt {
/** \addtogroup dr_evt_params
 *  @{ */

struct cmd_line_opts {
    std::string m_all_setup;
    std::string m_sim_setup;
    std::string m_trace_setup;
    bool m_is_set;

    bool parse_cmd_line(int argc, char** argv);
    void show() const;
};

/**@}*/
} // end of namespace dr_evt

#endif // DR_EVT_PARAMS_DR_EVT_PARAMS_HPP
