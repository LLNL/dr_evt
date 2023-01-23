/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef  DR_EVT_PROTO_DR_EVT_PARAMS_HPP
#define  DR_EVT_PROTO_DR_EVT_PARAMS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if !defined(DR_EVT_HAS_PROTOBUF)
#error DR_EVT requires protocol buffer
#endif

#include <string>
#include "proto/dr_evt_params.pb.h"
#include "params/sim_params.hpp"
#include "params/trace_params.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_proto
 *  @{ */

void read_proto_params(const std::string& filename,
                       dr_evt::Sim_Params& sp, bool verbose = false);

void read_proto_params(const std::string& filename,
                       dr_evt::Trace_Params& sp, bool verbose = false);

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_PROTO_DR_EVT_PARAMS_HPP
