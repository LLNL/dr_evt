/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef  DR_EVT_PROTO_UTILS_HPP
#define  DR_EVT_PROTO_UTILS_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if !defined(DR_EVT_HAS_PROTOBUF)
#error DR_EVT requires protocol buffer
#endif

#include <string>
#include <iostream>
#include <fstream>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>

namespace dr_evt {
/** \addtogroup dr_evt_proto
 *  @{ */

void pbuf_log_collector(
       google::protobuf::LogLevel level,
       const char* filename,
       int line,
       const std::string& message);

template<typename T>
bool read_prototext(const std::string& file_name, const bool is_binary,
                    T& dr_evt_proto_params)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  std::ifstream input(file_name, std::ios::in | std::ios::binary);

  google::protobuf::SetLogHandler(pbuf_log_collector);

  if (!input) {
    std::cerr << file_name << ": File not found!" << std::endl;
    return false;
  }
  if (is_binary) {
    if (!dr_evt_proto_params.ParseFromIstream(&input)) {
      std::cerr << "Failed to parse DR_EVT_Params in binary-formatted input file: "
                << file_name << std::endl;
      return false;
    }
  } else {
    google::protobuf::io::IstreamInputStream istrm(&input);
    if (!google::protobuf::TextFormat::Parse(&istrm, &dr_evt_proto_params)) {
      std::cerr << "Failed to parse DR_EVT_Params in text-formatted input file: "
                << file_name << std::endl;
      return false;
    }
  }
  return true;
}

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_PROTO_UTILS_HPP
