/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#ifndef DR_EVT_PROTO_DR_EVT_PARAMS_HPP
#define DR_EVT_PROTO_DR_EVT_PARAMS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if !defined(DR_EVT_HAS_PROTOBUF)
#error DR_EVT requires protocol buffer
#endif

#include "params/sim_params.hpp"
#include "params/trace_params.hpp"
#include "proto/dr_evt_params.pb.h"
#include <string>

namespace dr_evt {
/** \addtogroup dr_evt_proto
 *  @{ */

/** @brief Read protobuf simulation configuration into Sim_Params.
 * @param[in] filename Prototext or binary configuration filename.
 * @param[out] sp Destination simulation parameters.
 * @param[in] verbose true to emit parsing diagnostics.
 * @throws std::runtime_error if the file cannot be opened or parsed, or
 *         contains unsupported configuration values. */
void read_proto_params(const std::string &filename, dr_evt::Sim_Params &sp,
                       bool verbose = false);

/** @brief Read protobuf trace-tool configuration into Trace_Params.
 * @param[in] filename Prototext or binary configuration filename.
 * @param[out] sp Destination trace parameters.
 * @param[in] verbose true to emit parsing diagnostics.
 * @throws std::runtime_error if the file cannot be opened or parsed. */
void read_proto_params(const std::string &filename, dr_evt::Trace_Params &sp,
                       bool verbose = false);

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_PROTO_DR_EVT_PARAMS_HPP
